#include "xx_client.hpp"
#include "mem_sem.h"
#include "table_defs.h"
#include "tar.h"

#define XX_CLIENT_INT_H_
#include "xx_client_int.h"
#undef XX_CLIENT_INT_H_

#include <yt/cpp/mapreduce/client/client.h>
#include <yt/cpp/mapreduce/common/retry_lib.h>
#include <yt/cpp/mapreduce/interface/error_codes.h>
#include <yt/cpp/mapreduce/interface/logging/logger.h>
#include <yt/cpp/mapreduce/util/wait_for_tablets_state.h>
#include <yt/cpp/mapreduce/util/ypath_join.h>

#include <library/cpp/blockcodecs/core/codecs.h>
#include <library/cpp/logger/global/global.h>
#include <library/cpp/regex/pcre/regexp.h>
#include <library/cpp/retry/retry.h>
#include <library/cpp/threading/cancellation/cancellation_token.h>
#include <library/cpp/threading/future/subscription/wait_any.h>
#include <library/cpp/ucompress/reader.h>
#include <library/cpp/ucompress/writer.h>
#include <library/cpp/yson/node/node_io.h>

#include <util/digest/city_streaming.h>
#include <util/folder/path.h>
#include <util/generic/guid.h>
#include <util/generic/scope.h>
#include <util/generic/size_literals.h>
#include <util/memory/blob.h>
#include <util/stream/format.h>
#include <util/stream/printf.h>
#include <util/string/builder.h>
#include <util/string/join.h>
#include <util/system/env.h>
#include <util/system/filemap.h>
#include <util/system/hostname.h>
#include <util/system/tempfile.h>
#include <util/system/thread.h>
#include <util/thread/pool.h>

#include <utility>

using namespace NYT;

// see .pyx
extern void YaYtStoreLoggingHook(ELogPriority priority, const char* msg, size_t size);
extern void YaYtStoreDisableHook(void* callback, const TString& errorType, const TString& errorMessage);
extern void YaYtStoreStartStage(void* owner, const TString& name);
extern void YaYtStoreFinishStage(void* owner, const TString& name);

namespace {
    std::atomic_bool exiting{};

    template <class F, class... Args>
    auto CallHook(F&& func, Args&&... args) {
        // When the python interpreter is terminating, not only calling python code but even getting GIL can cause a segfault
        if (exiting.load(std::memory_order::relaxed)) {
            return;
        }
        return std::invoke(std::forward<F>(func), std::forward<Args>(args)...);
    }
}

namespace NYa {
    void AtExit() {
        exiting.store(true, std::memory_order::relaxed);
    }
}

namespace {
    struct LogBridge: public NYT::ILogger {
        inline static ELevel CutLevel_ = ELevel::ERROR;
        virtual void Log(ELevel level, const ::TSourceLocation& sourceLocation, const char* format, va_list args) override {
            const auto level_string = [](ELevel level) -> const char* {
                switch (level) {
                    case ILogger::FATAL:
                        return "FATAL";
                    case ILogger::ERROR:
                        return "WARN";
                    case ILogger::INFO:
                        return "INFO";
                    case ILogger::DEBUG:
                        return "DEBUG";
                }
                Y_UNREACHABLE();
            };

            if (level <= CutLevel_) {
                TString msg;
                TStringOutput out(msg);
                Printf(out, "%s (%s:%i) ", level_string(level), sourceLocation.File.data(), sourceLocation.Line);
                Printf(out, format, args);
                CallHook(YaYtStoreLoggingHook, ELogPriority::TLOG_DEBUG, msg.data(), msg.length());
            }
        }
    };

    struct TLogBridgeBackend : public TLogBackend {
    public:
        TLogBridgeBackend() = default;
        ~TLogBridgeBackend() override = default;

        void WriteData(const TLogRecord& record) override {
            CallHook(YaYtStoreLoggingHook, record.Priority, record.Data, record.Len);
        }

        void ReopenLog() override {
        }
    };

    inline TStringBuf GetBaseName(TStringBuf string) {
        return string.RNextTok(LOCSLASH_C);
    }

    struct TLogBridgeFormatter : public ILoggerFormatter {
        void Format(const TLogRecordContext& ctx, TLogElement& elem) const override {
            elem << "(" << GetBaseName(ctx.SourceLocation.File) << ":" << ctx.SourceLocation.Line << ") ";
        };
    };

}

#define Y_ENSURE_FATAL(condition, message) Y_ENSURE_EX(condition, TWithBackTrace<NYa::TYtStoreFatalError>() << message)

namespace NYa {
    // Note: overrides week functions declared in devtools/ya/cpp/entry/entry.cpp
    void InitYt(int argc, char** argv) {
        TString oldLogLevel = NYT::TConfig::Get()->LogLevel;
        // Tune logging only if the process doesn't execute YT-operation
        if (GetEnv("YT_JOB_ID").empty()) {
            // YT_LOG_LEVEL=debug may by usefull to debug problems with yt_store,
            // but NYT::Initialize() writes debug messages to the console.
            // It's better to disable the logger until yt_store configures it properly.
            // It's a little bit tricky (see comments below).

            // Disable the YT internal core logger
            NYT::TConfig::Get()->LogUseCore = false;

            // Prohibit the TStdErrLogger write debug messages to the console.
            // JoblessInitialize() set this logger as a current one before does anything.
            NYT::TConfig::Get()->LogLevel = "error";

        }

        NYT::Initialize(argc, argv);

        // Now we can replace TStdErrLogger by TNullLogger and restore the log level
        NYT::SetLogger(nullptr);
        NYT::TConfig::Get()->LogLevel = oldLogLevel;
    }

    void InitializeLogger() {
        static bool already = false;
        if (!already) {
            // intrusive ptr will try to dealloc pointer so we need to malloc
            NYT::SetLogger(new LogBridge());

            // Woudnt be here if logLevelStr invalid
            TryFromString(to_lower(TConfig::Get()->LogLevel), LogBridge::CutLevel_);

            DoInitGlobalLog(MakeHolder<TLogBridgeBackend>(), MakeHolder<TLogBridgeFormatter>());

            already = true;
        }
    }

    IYtClusterConnectorPtr YtClusterConnectorPtr{};

    namespace {
        using TYtTimestamp = ui64;
        using TKnownHashesSet = THashSet<TString>;

        TYtTimestamp ToYtTimestamp(TInstant time) {
            return time.MicroSeconds();
        }

        TInstant FromYtTimestamp(TYtTimestamp ts) {
            return TInstant::MicroSeconds(ts);
        }

        template <class T>
        T SafeChildAs(const NYT::TNode& node, const TString& child) {
            const auto& childNode = node.At(child);
            return childNode.IsOfType<T>() ? childNode.As<T>() : T{};
        }

        class TChunkFetcher: public IWalkInput, public NTar::IUntarInput {
        public:
            using TFetchDataFunc = std::function<NYT::TNode::TListType(const TVector<ui64>&)>;
            TChunkFetcher(TFetchDataFunc fetchDataFunc, ui64 chunksCount, ui64 dataSize)
                : FetchDataFunc_{fetchDataFunc}
                , ChunksCount_{chunksCount}
            {
                Y_ENSURE(ChunksCount_ > 0);
                Fetch(0, ChunksCount_ - 1);
                if (ChunksCount_ == 1) {
                    RealHash_ = CityHash64(Chunks_[0].ChildAsString("data"));
                } else {
                    const auto& first = Chunks_[0].ChildAsString("data");
                    const auto& last = Chunks_[ChunksCount_ - 1].ChildAsString("data");
                    if (last.size() >= 64) {
                        StreamingCityHash_.emplace(dataSize, first.c_str(), last.c_str() + last.size() - 64);
                    } else {
                        if (ChunksCount_ > 2) {
                            Fetch(ChunksCount_ - 2);
                        }
                        const auto& pre_last = Chunks_[ChunksCount_ - 2].ChildAsString("data");
                        char tail[64];
                        memcpy(tail, pre_last.c_str() + pre_last.size() - 64 + last.size(), 64 - last.size());
                        memcpy(tail + 64 - last.size(), last.c_str(), last.size());
                        StreamingCityHash_.emplace(dataSize, first.c_str(), tail);
                    }
                }
            }

            size_t DoUnboundedNext(const void** ptr) override {
                if (ChunkNo_ > 0) {
                    Chunks_.erase(ChunkNo_ - 1);
                }
                if (ChunkNo_ == ChunksCount_) {
                    *ptr = nullptr;
                    return 0;
                }
                Fetch(ChunkNo_);
                const auto& cur = Chunks_[ChunkNo_].ChildAsString("data");
                if (StreamingCityHash_.has_value()) {
                    StreamingCityHash_->Process(cur.c_str(), cur.size());
                }
                ++ChunkNo_;
                if (ChunkNo_ == ChunksCount_ && StreamingCityHash_.has_value()) {
                    RealHash_ = (*StreamingCityHash_)();
                }

                *ptr = cur.c_str();
                return cur.size();
            }

            size_t Read(const void** ptr) override {
                return DoUnboundedNext(ptr);
            }

            ui64 GetHash() const {
                Y_ENSURE(RealHash_, "Real hash is unknown yet");
                return RealHash_;
            }

        private:
            TFetchDataFunc FetchDataFunc_;
            ui64 ChunksCount_;
            ui64 ChunkNo_{};
            std::unordered_map<ui64, NYT::TNode> Chunks_{};
            std::optional<TStreamingCityHash64> StreamingCityHash_{};
            ui64 RealHash_{};

            void Fetch(ui64 chunk_i, ui64 chunk_j = Max<ui64>()) {
                TVector<ui64> requestedChunks;
                if (!Chunks_.contains(chunk_i)) {
                    requestedChunks.push_back(chunk_i);
                }
                if (chunk_j != Max<ui64>() && chunk_j != chunk_i && !Chunks_.contains(chunk_j)) {
                    requestedChunks.push_back(chunk_j);
                }
                if (requestedChunks.empty()) {
                    return;
                }
                TNode::TListType rows = FetchDataFunc_(requestedChunks);
                for (auto& row : FetchDataFunc_(requestedChunks)) {
                    Chunks_[row.ChildAsUint64("chunk_i")] = std::move(row);
                }
            }
        };

        class TChunkDecoder : public NTar::IUntarInput {
        public:
            TChunkDecoder(IInputStream& source)
                : Decoder_{&source}
            {
            }

            size_t Read(const void** buffer) override {
                *buffer = Buf_.Data();
                auto len_decoded = Decoder_.Read(Buf_.Data(), Buf_.Size());
                DecodedSize_ += len_decoded;
                return len_decoded;
            }

            size_t DecodedSize() const noexcept {
                return DecodedSize_;
            }

        private:
            NUCompress::TDecodedInput Decoder_;
            TTempBuf Buf_{1 << 20};
            size_t DecodedSize_{};
        };

        struct TStripRawStat {
            static inline const TString INITIAL_DATA_SIZE_COLUMN = "initial_data_size";
            static inline const TString REMAINING_DATA_SIZE_COLUMN = "remaining_data_size";
            static inline const TString INITIAL_MIN_ACCESS_TIME_COLUMN = "initial_min_access_time";
            static inline const TString REMAINING_MIN_ACCESS_TIME_COLUMN = "remaining_min_access_time";
            static inline const TString INITIAL_FILE_COUNT_COLUMN = "initial_file_count";
            static inline const TString REMAINING_FILE_COUNT_COLUMN = "remaining_file_count";

            NYT::TNode ToNode() {
                return NYT::TNode()
                    (INITIAL_DATA_SIZE_COLUMN, InitialDataSize)
                    (REMAINING_DATA_SIZE_COLUMN, RemainingDataSize)
                    (INITIAL_MIN_ACCESS_TIME_COLUMN, InitialMinAccessTime)
                    (REMAINING_MIN_ACCESS_TIME_COLUMN, RemainingMinAccessTime)
                    (INITIAL_FILE_COUNT_COLUMN, InitialFileCount)
                    (REMAINING_FILE_COUNT_COLUMN, RemainingFileCount);
            }

            void UpdateFromNode(const NYT::TNode& row) {
                InitialDataSize += row.ChildAsUint64(INITIAL_DATA_SIZE_COLUMN);
                RemainingDataSize += row.ChildAsUint64(REMAINING_DATA_SIZE_COLUMN);
                InitialMinAccessTime = Min(InitialMinAccessTime, row.ChildAs<TYtTimestamp>(INITIAL_MIN_ACCESS_TIME_COLUMN));
                RemainingMinAccessTime = Min(RemainingMinAccessTime, row.ChildAs<TYtTimestamp>(REMAINING_MIN_ACCESS_TIME_COLUMN));
                InitialFileCount += row.ChildAsUint64(INITIAL_FILE_COUNT_COLUMN);
                RemainingFileCount += row.ChildAsUint64(REMAINING_FILE_COUNT_COLUMN);
            }

            size_t InitialDataSize{};
            size_t RemainingDataSize{};
            TYtTimestamp InitialMinAccessTime{Max<TYtTimestamp>()};
            TYtTimestamp RemainingMinAccessTime{Max<TYtTimestamp>()};
            size_t InitialFileCount{};
            size_t RemainingFileCount{};

        };

        class TStripMapper final : public NYT::IMapper<NYT::TTableReader<NYT::TNode>, NYT::TTableWriter<NYT::TNode>> {
        public:
            TStripMapper() = default;

            TStripMapper(
                    TInstant now,
                    TDuration ttl,
                    const TVector<TNameReTtl>& nameReTtls,
                    const TVector<TString>& mappedColumns
            )
                : MappedColumns_{mappedColumns}
            {
                AccessTimeThreshold_ = ttl ? ToYtTimestamp(now - ttl) : 0;
                for (const auto& opt : nameReTtls) {
                    NameReAccessTimeThresholds_.emplace_back(opt.NameRe, ToYtTimestamp(now - opt.Ttl));
                }
            }

            void Do(TReader* reader, TWriter* writer) override {
                TVector<std::pair<TRegExMatch, TYtTimestamp>> reAccessTimeThresholds{};
                for (const auto& [reStr, threshold] : NameReAccessTimeThresholds_) {
                    reAccessTimeThresholds.emplace_back(TRegExMatch{reStr}, threshold);
                }

                for (auto& cursor : *reader) {
                    const NYT::TNode& row = cursor.GetRow();
                    NYT::TNode outRow{};
                    for (const TString& column : MappedColumns_) {
                        outRow[column] = row[column];
                    }
                    const NYT::TNode& accessTimeNode = row.At("access_time");
                    const NYT::TNode& nameNode = row.At("name");
                    bool remove = false;
                    if (!accessTimeNode.HasValue() || !nameNode.HasValue() || !row.At("data_size").HasValue()) {
                        remove = true;
                    } else {
                        const TYtTimestamp accessTime = accessTimeNode.As<TYtTimestamp>();
                        const TString& name = nameNode.AsString();
                        remove = accessTime < AccessTimeThreshold_;
                        for (const auto& [re, threshold] : reAccessTimeThresholds) {
                            if (re.Match(name.c_str())) {
                                remove = accessTime < threshold;
                                break;
                            }
                        }
                    }
                    outRow["remove"] = remove;
                    writer->AddRow(std::move(outRow));
                }
            }

            Y_SAVELOAD_JOB(AccessTimeThreshold_, NameReAccessTimeThresholds_, MappedColumns_);
        private:
            TYtTimestamp AccessTimeThreshold_{};
            TVector<std::pair<TString, TYtTimestamp>> NameReAccessTimeThresholds_{};
            TVector<TString> MappedColumns_;
        };
        REGISTER_MAPPER(TStripMapper);

        class TStripReducer final : public NYT::IReducer<NYT::TTableReader<NYT::TNode>, NYT::TTableWriter<NYT::TNode>> {
        public:
            static inline const TString LAST_REF_COLUMN = "last_ref";

            TStripReducer() = default;
            TStripReducer(const TVector<TString> metadataKeyColumns, bool writeRemainingRows)
                : MetadataKeyColumns_{metadataKeyColumns}
                , WriteRemainingRows_{writeRemainingRows}
            {
            }

            void Do(TReader* reader, TWriter* writer) override {
                NYT::TNode firstRow{};
                TVector<NYT::TNode> remainingRows{};
                bool allRowsRemoved = true;

                for (auto& cursor : *reader) {
                    const NYT::TNode& row = cursor.GetRow();
                    if (!firstRow.HasValue()) {
                        firstRow = row;
                    }
                    bool remove = row.ChildAsBool("remove");
                    allRowsRemoved = allRowsRemoved && remove;
                    if (const NYT::TNode& node = row.At("access_time"); node.HasValue()) {
                        TYtTimestamp accessTime = node.As<TYtTimestamp>();
                        if (StripStat_.InitialMinAccessTime > accessTime) {
                            StripStat_.InitialMinAccessTime = accessTime;
                        }
                        if (!remove && StripStat_.RemainingMinAccessTime > accessTime) {
                            StripStat_.RemainingMinAccessTime = accessTime;
                        }
                    }
                    if (remove) {
                        NYT::TNode outRow{};
                        for (const TString& column : MetadataKeyColumns_) {
                            outRow[column] = row[column];
                        }
                        writer->AddRow(outRow);
                    } else if (WriteRemainingRows_) {
                        remainingRows.push_back(row);
                    }
                }

                ++StripStat_.InitialFileCount;
                if (const NYT::TNode& node = firstRow.At("data_size"); node.HasValue()) {
                    size_t dataSize = node.AsUint64();
                    StripStat_.InitialDataSize += dataSize;
                    if (!allRowsRemoved) {
                        StripStat_.RemainingDataSize += dataSize;
                    }
                }
                if (allRowsRemoved) {
                    writer->AddRow(
                        NYT::TNode()
                            ("hash", firstRow["hash"])
                            ("chunks_count", firstRow["chunks_count"])
                    );
                } else {
                    ++StripStat_.RemainingFileCount;
                }

                if (!remainingRows.empty()) {
                    for (NYT::TNode& row : remainingRows) {
                        // Remove some useless columns
                        row.AsMap().erase("name");
                        row.AsMap().erase("remove");
                    }
                    remainingRows.back()[LAST_REF_COLUMN] = true;
                    writer->AddRowBatch(remainingRows, REMAINING_ROWS_TABLE_INDEX);
                }
            }

            void Finish(TWriter* writer) override {
                writer->AddRow(StripStat_.ToNode(), STAT_TABLE_INDEX);
            }

            Y_SAVELOAD_JOB(MetadataKeyColumns_, WriteRemainingRows_);
        private:
            static constexpr size_t STAT_TABLE_INDEX = 1;
            static constexpr size_t REMAINING_ROWS_TABLE_INDEX = 2;

            TVector<TString> MetadataKeyColumns_;
            bool WriteRemainingRows_;
            TStripRawStat StripStat_{};
        };
        REGISTER_REDUCER(TStripReducer)

        class TFirstRowReducer final : public NYT::IReducer<NYT::TTableReader<NYT::TNode>, NYT::TTableWriter<NYT::TNode>> {
        public:
            TFirstRowReducer() = default;

            void Do(TReader* reader, TWriter* writer) override {
                writer->AddRow(reader->GetRow());
            }
        };
        REGISTER_REDUCER(TFirstRowReducer)

        class TOrphanHashesExtractReducer : public NYT::IReducer<NYT::TTableReader<NYT::TNode>, NYT::TTableWriter<NYT::TNode>> {
        static constexpr ui32 KNOWN_HASHES_TABLE_INDEX = 1;
        public:
            TOrphanHashesExtractReducer() = default;

            TOrphanHashesExtractReducer(TYtTimestamp timeThreshold)
                : TimeThreshold_{timeThreshold}
            {
            }

            void Do(NYT::TTableReader<NYT::TNode>* reader, NYT::TTableWriter<NYT::TNode>* writer) override {
                NYT::TNode::TListType outRows;
                for (auto& cursor : *reader) {
                    if (cursor.GetTableIndex() == KNOWN_HASHES_TABLE_INDEX) {
                        return;  // Not an orphan row
                    }
                    const NYT::TNode& row = cursor.GetRow();
                    // Data row is too young to be deleted
                    if (row.ChildAs<TYtTimestamp>("create_time") > TimeThreshold_) {
                        return;
                    }
                    outRows.push_back(NYT::TNode()("hash", row["hash"])("chunk_i", row["chunk_i"]));
                }
                writer->AddRowBatch(outRows);
            }

            Y_SAVELOAD_JOB(TimeThreshold_);

        private:
            TYtTimestamp TimeThreshold_;
        };
        REGISTER_REDUCER(TOrphanHashesExtractReducer);
    }

    class TYtStore::TImpl {
    public:
        TImpl(const TString& proxy, const TString& dataDir, const TYtStoreOptions& options)
            : ConnectOptions_{options.ConnectOptions}
            , Owner_{options.Owner}
            , ReadOnly_{options.ReadOnly}
            , CheckSize_{options.CheckSize}
            , MaxCacheSize_{options.MaxCacheSize}
            , Ttl_{options.Ttl}
            , NameReTtls_{options.NameReTtls}
            , OperationPool_{options.OperationPool}
            , RetryTimeLimit_{options.RetryTimeLimit}
            , PrepareTimeout_{options.PrepareTimeout}
            , ProbeBeforePut_{options.ProbeBeforePut}
            , ProbeBeforePutMinSize_{options.ProbeBeforePutMinSize}
            , CritLevel_{options.CritLevel}
            , GSID_{options.GSID}
        {
            ThreadPool_.Start();
            ThreadPool_.SetMaxIdleTime(TDuration::Seconds(2));

            // Validate
            for (const TNameReTtl& item : NameReTtls_) {
                ValidateRegexp(item.NameRe);
            }

            std::size_t maxRetries = RETRY_MAX_COUNT;
            auto maxTime = TDuration::Max();
            if (RetryTimeLimit_) {
                maxRetries = std::numeric_limits<std::size_t>::max();
                maxTime = options.RetryTimeLimit;
                if (options.InitTimeout && options.InitTimeout < RetryTimeLimit_) {
                    WARNING_LOG << "init timeout (" <<  HumanReadable(options.InitTimeout) <<
                        ") is less than retry time limit (" << HumanReadable(RetryTimeLimit_) << ")";
                }
                if (PrepareTimeout_ && PrepareTimeout_ < RetryTimeLimit_) {
                        WARNING_LOG << "prepare timeout (" <<  HumanReadable(PrepareTimeout_) <<
                            ") is less than retry time limit (" << HumanReadable(RetryTimeLimit_) << ")";
                }
            }
            auto retryClassFunction = [this] (const yexception& e) {
                if (!Disabled_ && IsYtError(e) && !IsPermanentError(e)) {
                    return ERetryErrorClass::ShortRetry;
                }
                return ERetryErrorClass::NoRetry;
            };
            RetryPolicy_ = TRetryPolicy::GetExponentialBackoffPolicy(
                std::move(retryClassFunction),
                RETRY_MIN_DELAY,
                RETRY_MIN_DELAY, // not used
                RETRY_MAX_DELAY,
                maxRetries,
                maxTime,
                RETRY_SCALE_FACTOR
            );

            TDuration initTimeout{};
            if (options.InitTimeout) {
                initTimeout = options.InitTimeout;
            } else if (RetryTimeLimit_) {
                initTimeout = RetryTimeLimit_ + TDuration::MilliSeconds(500);
            } else if (ReadOnly_ && CritLevel_ == ECritLevel::NONE) {
                initTimeout = DEFAULT_INTERACTIVE_INIT_TIMEOUT;
            } else {
                initTimeout = DEFAULT_BG_INIT_TIMEOUT;
            }

            auto promise = InitializeControl_.Init(initTimeout);
            ThreadPool_.SafeAddFunc([this, promise=std::move(promise), proxy, dataDir]() mutable {
                TThread::SetCurrentThreadName("YtStore::Initialize");
                Initialize(std::move(promise), proxy, dataDir);
            });
            if (CritLevel_ != ECritLevel::NONE) {
                // Wait for the initialization to complete
                InitializeControl_.GetValue();
            }
        }

        ~TImpl() = default;

        bool Disabled() const noexcept {
            return Disabled_.load(std::memory_order::relaxed);
        }

        bool ReadOnly() const noexcept {
            return ReadOnly_.load(std::memory_order::relaxed);
        }

        void Prepare(TPrepareOptionsPtr options) {
            CheckDisabledAndDisableOnError([&]() {
                TDuration prepareTimeout{};
                if (PrepareTimeout_) {
                    prepareTimeout = PrepareTimeout_;
                } else if (RetryTimeLimit_) {
                    prepareTimeout = RetryTimeLimit_ + TDuration::MilliSeconds(500);
                } else if (ReadOnly_ && CritLevel_ == ECritLevel::NONE) {
                    prepareTimeout = DEFAULT_INTERACTIVE_PREPARE_TIMEOUT;
                } else {
                    prepareTimeout = DEFAULT_BG_PREPARE_TIMEOUT;
                }
                auto deadLine = prepareTimeout.ToDeadLine();
                auto promise = PrepareControl_.Init();

                ThreadPool_.SafeAddFunc([this, promise=std::move(promise), options, deadLine]() mutable {
                    TThread::SetCurrentThreadName("YtStore::Prepare");
                    DoPrepare(std::move(promise), options, deadLine);
                });
                // To support refresh metadata only mode (--yt-store-refresh-on-read --threads=0)
                // we must wait for DoPrepare result
                if (options->RefreshOnRead) {
                    PrepareControl_.GetValue();
                }
            });
        }

        bool Has(const TString& uid) {
            return CheckDisabledAndDisableOnError(false, [&] {
                Metrics_.SetTimeToFirstCallHas();
                TPrepareResultPtr prepareResultPtr = PrepareControl_.GetValue();
                bool has = prepareResultPtr->Meta.contains(uid);
                DEBUG_LOG << "YT Probing " << uid << " => " << (has ? "True" : "False");
                return has;
            });
        }

        bool TryRestore(const TString& uid, const TString& intoDir) {
            return CheckDisabledAndDisableOnError(false, [&] {
                TPrepareResultPtr prepareResultPtr = PrepareControl_.GetValue();
                DEBUG_LOG << "Try restore " << uid << " from YT";
                auto meta = prepareResultPtr->Meta.FindPtr(uid);

                try {
                    if (!meta) {
                        ythrow TYtStoreError() << "no metadata for uid";
                    }

                    auto codec = SafeChildAs<TString>(*meta, "codec");
                    if (codec == NO_DATA_CODEC) {
                        ythrow TYtStoreError() << "can't restore data with service codec '" << NO_DATA_CODEC << "'";
                    }

                    TString hash = SafeChildAs<TString>(*meta, "hash");
                    ui64 chunksCount = SafeChildAs<ui64>(*meta, "chunks_count");
                    ui64 dataSize = SafeChildAs<ui64>(*meta, "data_size");
                    if (hash.empty() || !chunksCount || !dataSize) {
                        ythrow TYtStoreError() << "malformed metadata for uid";
                    }

                    auto fetchDataFunc = [&](const TVector<ui64> chunks) {
                        NYT::TNode::TListType keys{};
                        for (auto chunk_i : chunks) {
                            keys.push_back(NYT::TNode()("hash", hash)("chunk_i", chunk_i));
                        }
                        auto rows = RetryYtError([&] {
                            return prepareResultPtr->Task->Client->LookupRows(
                                NYT::JoinYPaths(prepareResultPtr->Task->DataDir, DATA_TABLE),
                                keys,
                                TLookupRowsOptions().Timeout(DATA_LOOKUP_TIMEOUT).KeepMissingRows(true)
                            );
                        });
                        std::unique_ptr<TVector<ui64>> missingChunks{};
                        for (size_t i = 0; i < rows.size(); ++i) {
                            if (rows[i].IsNull()) {
                                ythrow TYtStoreError() << "hash=" << hash << " has at least one missing chunk: " << keys[i].ChildAsUint64("chunk_i");
                            }
                        }
                        return rows;
                    };

                    TChunkFetcher chunkFetcher{fetchDataFunc, chunksCount, dataSize};
                    NTar::IUntarInput* input{&chunkFetcher};
                    std::unique_ptr<TChunkDecoder> decoder{};
                    if (codec) {
                        decoder = std::make_unique<TChunkDecoder>(chunkFetcher);
                        input = decoder.get();
                    }
                    NTar::Untar(*input, intoDir);

                    auto realHash = ToString(chunkFetcher.GetHash());
                    if (realHash != hash) {
                        ythrow TYtStoreError() << "Hash mismatch: expected(" << hash << ") != real(" << realHash << ")";
                    }

                    Metrics_.IncDataSize("get", dataSize);
                    if (decoder) {
                        Metrics_.UpdateCompressionRatio(dataSize, decoder->DecodedSize());
                    }
                    if (meta->HasKey("cuid") && SafeChildAs<TString>(*meta, "cuid") == uid && meta->ChildAsString("uid") != uid) {
                        Metrics_.IncCounter("get-by-cuid");
                    }
                } catch (const std::exception& e) {
                    DEBUG_LOG << "Try restore " << uid <<" from YT failed: " << e.what();
                    Metrics_.CountFailures("get");

                    // Treat YT errors as permanent and proceed to disabling cache
                    if (IsYtError(e)) {
                        throw;
                    }
                    return false;
                }

                DEBUG_LOG << "Try restore " <<  uid << " from YT completed. Successfully restored " <<  meta->ChildAsString("name");
                return true;
            });
        }

        bool Put(const TPutOptions& options) {
            if (options.Files.empty()) {
                DEBUG_LOG << "Put (" << options.Uid << ") is failed: empty file list";
                return false;
            }

            TString name = options.Files[0].RelativeTo(options.RootDir);
            auto logPrefix = TStringBuilder() << "Put " << name << "(" << options.Uid << ") ";
            DEBUG_LOG << logPrefix << "to YT";

            if (ReadOnly() || Disabled()) {
                DEBUG_LOG << logPrefix << "to YT rejected because of readonly or disabled storage";
                return false;
            }

            try {
                TConfigureResultPtr config = InitializeControl_.GetValue();
                TPrepareResultPtr prepareResultPtr = PrepareControl_.GetValue();
                if (prepareResultPtr->Meta.contains(options.Uid)) {
                    // Should never happen
                    DEBUG_LOG << logPrefix << "to YT completed(no-op)";
                    return true;
                }
                // Yndexer support
                if (options.Codec == NO_DATA_CODEC) {
                    WriteMeta(
                        config,
                        options.SelfUid,
                        options.Uid,
                        "" /* cuid */,
                        name,
                        options.Codec,
                        options.ForcedSize,
                        "" /* hash */,
                        0 /* chunksCount */
                    );
                    return true;
                }

                if (config->Version >= 3 && options.Cuid && prepareResultPtr->Meta.contains(options.Cuid)) {
                    const auto& meta = prepareResultPtr->Meta.at(options.Cuid);
                    // Doing additional check in case the content uid is clashed in a some way
                    if (meta.ChildAsString("name") == name && meta.ChildAsString("self_uid") == options.SelfUid) {
                        WriteMeta(
                            config,
                            options.SelfUid,
                            options.Uid,
                            options.Cuid,
                            name,
                            meta.ChildAsString("codec"),
                            meta.ChildAsUint64("data_size"),
                            meta.ChildAsString("hash"),
                            meta.ChildAsUint64("chunks_count")
                        );
                        return true;
                    }
                }

                ui64 rawFilesSize = 0;
                for (const auto& f : options.Files) {
                    rawFilesSize += TFileStat{f, true}.Size;
                }
                if (ProbeBeforePut_ && rawFilesSize >= ProbeBeforePutMinSize_) {
                    try {
                        if (ProbeMeta(config, prepareResultPtr, options.SelfUid, options.Uid)) {
                            DEBUG_LOG << logPrefix << "to YT completed(no-op): uid already exists";
                            return true;
                        }
                    } catch (...) {
                        DEBUG_LOG << "meta probing failed with error: " << CurrentExceptionMessage();
                    }
                }

                TFsPath archivePath{MakeTempName()};
                PrepareData(archivePath, options.RootDir, options.Files, options.Codec);
                auto archiveData = TBlob::FromFile(archivePath);

                ui64 encodedDataSize = archiveData.size();
                ui64 maxBatchSize = YT_CACHE_CELL_LIMIT * YT_CELLS_PER_INSERT_LIMIT;

                auto memGuard = MemSem_.Acquire(Min(encodedDataSize, maxBatchSize));

                TString hash = ToString(CityHash64(archiveData));

                ui64 chunksCount = 0;
                TYtTimestamp createTime = ToYtTimestamp(TInstant::Now());
                for (auto data = archiveData.AsStringBuf(); !data.empty(); ) {
                    NYT::TNode::TListType dataRows{};
                    while (!data.empty() && dataRows.size() < YT_CELLS_PER_INSERT_LIMIT) {
                        auto chunk = data.substr(0, YT_CACHE_CELL_LIMIT);
                        dataRows.push_back(
                            NYT::TNode()
                                ("hash", hash)
                                ("chunk_i", chunksCount)
                                ("create_time", createTime)
                                ("data", chunk)
                        );
                        data = data.substr(chunk.size());
                        ++chunksCount;
                    }
                    SafeInsertRows(config, DATA_TABLE, std::move(dataRows), {"hash", "chunk_i"});
                }

                WriteMeta(config, options.SelfUid, options.Uid, options.Cuid, name, options.Codec, encodedDataSize, hash, chunksCount);
                Metrics_.IncDataSize("put", encodedDataSize);
                if (options.Codec) {
                    Metrics_.UpdateCompressionRatio(encodedDataSize, rawFilesSize);
                }
                DEBUG_LOG << logPrefix << "size=" << encodedDataSize << " to YT completed";
                return true;
            } catch (const TYtStoreFatalError&) {
                throw;
            } catch (...) {
                ReadOnly_.store(true, std::memory_order::relaxed);
                if (CritLevel_ == ECritLevel::PUT) {
                    Disable(true);
                }
                Metrics_.CountFailures("put");
                DEBUG_LOG << logPrefix << "to YT failed: " << CurrentExceptionMessage();
            }
            return false;
        }

        TMetrics GetMetrics() const {
            return Metrics_.GetData();
        }

        void Strip() {
            auto config = InitializeControl_.GetValue();
            if (!Ttl_ && NameReTtls_.empty() && std::visit([](const auto& v) {return v == 0;}, MaxCacheSize_)) {
                INFO_LOG << "Neither ttl nor max_cache_size was specified. Nothing to do";
                return;
            }
            if (ReadOnly_) {
                INFO_LOG << "Strip runs in readonly mode (dry run)";
            }

            auto [opClient, opDataDir] = GetOpCluster(config);
            Y_ENSURE(opClient && opDataDir);

            size_t maxCacheSize = GetMaxCacheSize(opClient, opDataDir);

            INFO_LOG << "Desired max age: " << (Ttl_ ? ToString(HumanReadable(Ttl_)) : "-");
            for (const auto& item : NameReTtls_) {
                INFO_LOG << "Desired max age: " << HumanReadable(item.Ttl) << " for " << item.NameRe;
            }
            INFO_LOG << "Desired data size: " << (maxCacheSize ? ToString(HumanReadableSize(maxCacheSize, ESizeFormat::SF_BYTES)) : "-");

            bool stripByDataSize = false;
            if (maxCacheSize) {
                size_t onDiskCacheSize = GetTablesSize(opClient, opDataDir);
                INFO_LOG << "Current size on disk: " << HumanReadableSize(onDiskCacheSize, ESizeFormat::SF_BYTES);
                stripByDataSize = onDiskCacheSize > maxCacheSize;
            }
            if (!stripByDataSize  && !Ttl_ && NameReTtls_.empty()) {
                INFO_LOG << "Nothing to do";
                return;
            }

            // Use transaction to automatically remove all temporary tables on transaction abort
            auto tx = opClient->StartTransaction();

            TVector<TString> mappedColumns{config->MetadataKeyColumns};
            mappedColumns.insert(mappedColumns.end(), {"hash", "chunks_count", "name", "access_time", "data_size"});

            /// Create unique temporary directory for operation results
            NYT::TYPath tmpRoot = NYT::JoinYPaths(TMP_DIR, CreateGuidAsString());
            tx->Create(tmpRoot, NYT::ENodeType::NT_MAP);

            NYT::TYPath removeTable = NYT::JoinYPaths(tmpRoot, "remove");
            NYT::TYPath infoTable = NYT::JoinYPaths(tmpRoot, "info");
            NYT::TYPath remainingRowsTable = NYT::JoinYPaths(tmpRoot, "remaining");
            NYT::TRichYPath mrInputPath = NYT::TRichYPath(NYT::JoinYPaths(opDataDir, METADATA_TABLE))
                .Columns(mappedColumns);

            auto mrSpec = NYT::TMapReduceOperationSpec()
                .AddInput<NYT::TNode>(mrInputPath)
                .AddOutput<NYT::TNode>(removeTable)
                .AddOutput<NYT::TNode>(infoTable)
                .ReduceBy("hash")
                .SortBy({"hash", "access_time"});
            if (stripByDataSize) {
                mrSpec.AddOutput<NYT::TNode>(remainingRowsTable);
            }
            AddPool(mrSpec);
            TInstant now = TInstant::Now();
            auto mapper = MakeIntrusive<TStripMapper>(now, Ttl_, NameReTtls_, mappedColumns);
            auto reducer = MakeIntrusive<TStripReducer>(config->MetadataKeyColumns, stripByDataSize);
            DEBUG_LOG << "Start MapReduce";
            tx->MapReduce(mrSpec, mapper, reducer);

            // Calc Stat
            TStripRawStat rawStat{};
            for (auto reader = tx->CreateTableReader<NYT::TNode>(infoTable); reader->IsValid(); reader->Next()) {
                rawStat.UpdateFromNode(reader->GetRow());
            }
            TDuration currentAge = now - Min(now, FromYtTimestamp(rawStat.InitialMinAccessTime));

            INFO_LOG << "Current net data size: " << HumanReadableSize(rawStat.InitialDataSize * REPLICATION_FACTOR, ESizeFormat::SF_BYTES);
            INFO_LOG << "Current max age: " << HumanReadable(currentAge);

            DEBUG_LOG << "Start deleting rows by access time" << (ReadOnly_ ? " (dry run)" : "");

            size_t removedMetadataRowCount{};
            for (auto reader = tx->CreateTableReader<NYT::TNode>(removeTable); reader->IsValid(); ) {
                NYT::TNode::TListType deleteMetadataKeys{};
                NYT::TNode::TListType deleteDataKeys{};
                while (reader->IsValid() && deleteMetadataKeys.size() < REMOVE_BATCH_SIZE) {
                    const NYT::TNode& row = reader->GetRow();
                    if (row.HasKey("hash")) {
                        AddDataKeys(deleteDataKeys, row.ChildAsString("hash"), row.ChildAsUint64("chunks_count"));
                    } else {
                        NYT::TNode keys{};
                        for (const TString& column : config->MetadataKeyColumns) {
                            keys[column] = row.At(column);
                        }
                        deleteMetadataKeys.push_back(std::move(keys));
                        ++removedMetadataRowCount;
                    }
                    reader->Next();
                }

                if (!ReadOnly_) {
                    DeleteRows(config, deleteMetadataKeys, deleteDataKeys);
                }
            }

            // Clean by data size if needed
            if (stripByDataSize && rawStat.RemainingDataSize * REPLICATION_FACTOR > maxCacheSize) {
                DEBUG_LOG << "Start sorting of remaining rows";
                auto sortSpec = NYT::TSortOperationSpec()
                    .AddInput(remainingRowsTable)
                    .Output(remainingRowsTable)
                    .SortBy("access_time");
                AddPool(sortSpec);
                tx->Sort(sortSpec);

                DEBUG_LOG << "Start deleting rows by data size" << (ReadOnly_ ? " (dry run)" : "");
                for (auto reader = tx->CreateTableReader<NYT::TNode>(remainingRowsTable); reader->IsValid(); ) {
                    NYT::TNode::TListType deleteMetadataKeys{};
                    NYT::TNode::TListType deleteDataKeys{};
                    bool done = false;
                    while (reader->IsValid() && deleteMetadataKeys.size() < REMOVE_BATCH_SIZE) {
                        const NYT::TNode& row = reader->GetRow();
                        NYT::TNode keys{};
                        for (const TString& column : config->MetadataKeyColumns) {
                            keys[column] = row.At(column);
                        }
                        deleteMetadataKeys.push_back(std::move(keys));
                        ++removedMetadataRowCount;

                        if (row.HasKey(TStripReducer::LAST_REF_COLUMN)) {
                            // The last row in the metadata table with the hash
                            AddDataKeys(deleteDataKeys, row.ChildAsString("hash"), row.ChildAsUint64("chunks_count"));
                            --rawStat.RemainingFileCount;
                            rawStat.RemainingDataSize -= row.ChildAsUint64("data_size");
                        }

                        reader->Next();

                        if (rawStat.RemainingDataSize * REPLICATION_FACTOR <= maxCacheSize) {
                            if (reader->IsValid()) {
                                const NYT::TNode& oldestRemainingRow = reader->GetRow();
                                rawStat.RemainingMinAccessTime = oldestRemainingRow.ChildAs<TYtTimestamp>("access_time");
                            } else {
                                // All rows are deleted
                                rawStat.RemainingMinAccessTime = ToYtTimestamp(now);
                            }
                            done = true;
                            break;
                        }
                    }

                    if (!ReadOnly_) {
                        DeleteRows(config, deleteMetadataKeys, deleteDataKeys);
                    }

                    if (done) {
                        break;
                    }
                }
            } else {
                INFO_LOG << "Cleaning by data size is not needed";
            }

            TDuration remainingAge = now - Min(now, FromYtTimestamp(rawStat.RemainingMinAccessTime));
            INFO_LOG << "Data size after clean: " << HumanReadableSize(rawStat.RemainingDataSize * REPLICATION_FACTOR, ESizeFormat::SF_BYTES);
            INFO_LOG << "Max age after clean: " << HumanReadable(remainingAge);
            INFO_LOG << "Removed row count from metadata: " << removedMetadataRowCount;
            INFO_LOG << "Removed hash count from data: " << (rawStat.InitialFileCount - rawStat.RemainingFileCount);
            INFO_LOG << "Removed net data size: " << HumanReadableSize((rawStat.InitialDataSize - rawStat.RemainingDataSize) * REPLICATION_FACTOR, ESizeFormat::SF_BYTES);

            // Put cleaner stat
            auto makeStatNode = [](TDuration ttl, size_t dataSize, size_t fileCount) {
                return NYT::TNode()
                ("max_age", static_cast<i64>(ttl.Seconds()))
                ("data_size", static_cast<i64>(dataSize))
                ("file_count", static_cast<i64>(fileCount));
            };
            NYT::TNode statNode = NYT::TNode()
                ("before_clean", makeStatNode(currentAge, rawStat.InitialDataSize, rawStat.InitialFileCount))
                ("after_clean", makeStatNode(remainingAge, rawStat.RemainingDataSize, rawStat.RemainingFileCount));
            PutStat(config, "cleaner", statNode);
            // Yes, it's the default behaviour and is optional but added to demonstrate intent
            tx->Abort();
        }

        void DataGc(const TDataGcOptions& options) {
            auto config = InitializeControl_.GetValue();
            if (ReadOnly_) {
                INFO_LOG << "Data GC runs in readonly mode (dry run)";
            }
            auto dataGcStartTime = TInstant::Now();

            auto [opClient, opDataDir] = GetOpCluster(config);
            Y_ENSURE(opClient && opDataDir);

            const NYT::TYPath dataTable = NYT::JoinYPaths(opDataDir, DATA_TABLE);
            const auto dataAttrsGetOpts = NYT::TGetOptions()
                .ReadFrom(NYT::EMasterReadKind::Cache)
                .AttributeFilter(TAttributeFilter().Attributes({"pivot_keys", "schema", "uncompressed_data_size"}));
            const NYT::TNode dataAttrs = opClient->Get(NYT::JoinYPaths(dataTable, "@"), dataAttrsGetOpts);
            const auto tx = opClient->StartTransaction();

            // Prepare table with known hashes
            const NYT::TNode& dataSchema = dataAttrs["schema"];
            Y_ENSURE(dataSchema.Size() > 2);
            const NYT::TTableSchema knownHashesSchema = NYT::TTableSchema::FromNode(NYT::TNode::CreateList({dataSchema[0], dataSchema[1]})).UniqueKeys(true);
            const NYT::TTableSchema unsortedKnownHashesSchema = NYT::TTableSchema(knownHashesSchema).UniqueKeys(false).SortBy({});
            const NYT::TYPath knownHashesTable = NYT::JoinYPaths(TMP_DIR, CreateGuidAsString());

            auto mrSpec = NYT::TMapReduceOperationSpec()
                .AddInput<NYT::TNode>(NYT::TRichYPath(NYT::JoinYPaths(opDataDir, METADATA_TABLE)).Columns({"hash"}))
                .AddOutput<NYT::TNode>(NYT::TRichYPath(knownHashesTable).Schema(unsortedKnownHashesSchema))
                .ReduceBy("hash");
            AddPool(mrSpec);
            DEBUG_LOG << "Start MapReduce over metadata table";
            tx->MapReduce(mrSpec, nullptr, MakeIntrusive<TFirstRowReducer>());

            // To do join-reduce operation between the the knownHashesSchema and data tables they must be sorted by the same columns
            DEBUG_LOG << "Start sort of known hashes table";
            auto sortSpec = NYT::TSortOperationSpec()
            .AddInput(knownHashesTable)
            .Output(NYT::TRichYPath(knownHashesTable).Schema(knownHashesSchema))
            .SortBy({"tablet_hash", "hash"});
            AddPool(sortSpec);
            tx->Sort(sortSpec);

            // The data table is too large to scan it in a single operation.
            // Split it by key ranges into reasonable parts.
            TVector<TKeyRange> keyRanges;
            const NYT::TNode::TListType& pivotKeys = dataAttrs["pivot_keys"].AsList();
            ui64 begin = 0;
            if (pivotKeys.size() > 1 && options.DataSizePerKeyRange > 0) {
                double lastPivotBegin = pivotKeys.back()[0].AsUint64();
                double maxKeyValue = lastPivotBegin + lastPivotBegin / (pivotKeys.size() - 1);
                auto rangeCount = Max<i64>(dataAttrs.ChildAsInt64("uncompressed_data_size") / options.DataSizePerKeyRange, 1);
                double step = maxKeyValue / rangeCount;
                for (int i = 1; i < rangeCount; i++) {
                    ui64 end = step * i;
                    keyRanges.push_back({begin, end});
                    begin = end;
                }
            }
            keyRanges.push_back({begin, {}});
            INFO_LOG << "Data table scanning split into " << keyRanges.size() << " operations";

            const TYtTimestamp timeThreshold = ToYtTimestamp(TInstant::Now() - DATA_GC_MIN_AGE);
            DEBUG_LOG << "Time threshold=" <<  timeThreshold;

            size_t deletedRowCount = 0;
            // It's ok to reuse the same table for all operations
            NYT::TYPath orphanRowsTable = NYT::JoinYPaths(TMP_DIR, CreateGuidAsString());
            for (size_t range_i = 0; range_i < keyRanges.size(); ++range_i) {
                auto startTime = TInstant::Now();
                const TKeyRange& keyRange = keyRanges[range_i];
                INFO_LOG << "Range " << keyRange << ": start " << (range_i + 1) << "/" << keyRanges.size();
                NYT::TRichYPath richDataPath = NYT::TRichYPath(dataTable)
                    .Columns({"tablet_hash", "hash", "chunk_i", "create_time"})
                    .AddRange(keyRange.AsReadRange());
                NYT::TRichYPath richKnownHashesPath = NYT::TRichYPath(knownHashesTable)
                    .Foreign(true);
                auto reduceSpec = NYT::TReduceOperationSpec()
                    .AddInput<NYT::TNode>(richDataPath)
                    .AddInput<NYT::TNode>(richKnownHashesPath)
                    .AddOutput<NYT::TNode>(orphanRowsTable)
                    .JoinBy({"tablet_hash", "hash"})
                    .EnableKeyGuarantee(false);
                if (options.DataSizePerJob > 0) {
                    reduceSpec.DataSizePerJob(options.DataSizePerJob);
                }
                AddPool(reduceSpec);

                auto reducer = MakeIntrusive<TOrphanHashesExtractReducer>(timeThreshold);
                tx->Reduce(reduceSpec, reducer);

                size_t foundRowCount = 0;
                for (auto reader = tx->CreateTableReader<NYT::TNode>(orphanRowsTable); reader->IsValid(); ) {
                    NYT::TNode::TListType keys;
                    for (; reader->IsValid() && keys.size() < REMOVE_BATCH_SIZE; reader->Next()) {
                        keys.push_back(std::move(reader->MoveRow()));
                    }
                    if (keys.empty())
                        break;
                    foundRowCount += keys.size();
                    if (!ReadOnly_) {
                        DeleteRows(config, {}, keys);
                    }
                }
                INFO_LOG << "Range " << keyRange << ": " << foundRowCount << " orphan rows found in " << HumanReadable(TInstant::Now() - startTime);
                deletedRowCount += foundRowCount;
            }

            if (deletedRowCount) {
                if (ReadOnly_) {
                    INFO_LOG << "Total: " << deletedRowCount << " orphan rows found";
                } else {
                    INFO_LOG << "Total: " << deletedRowCount << " orphan rows deleted";
                }
            } else {
                DEBUG_LOG << "No orphan rows found";
            }

            // Put data-gc stat
            auto statNode = NYT::TNode()
                ("deleted_row_count", static_cast<i64>(deletedRowCount))
                ("duration", static_cast<i64>((TInstant::Now() - dataGcStartTime).Seconds()));
            PutStat(config, "data_gc", statNode);
            // Yes, it's the default behaviour and is optional but added to demonstrate intent
            tx->Abort();
        }

        void PutStat(const TString& key, const TString& value) {
            TConfigureResultPtr config = InitializeControl_.GetValue();
            PutStat(config, key, NYT::NodeFromYsonString(value));
        }

        void Shutdown() noexcept {
            Disabled_ = true;
        }

        static inline void ValidateRegexp(const TString& re) {
            TRegExMatch x{re};
        }

        static inline void CreateTables(const TString& proxy, const TString& dataDir, const TCreateTablesOptions& options) {
            NYT::TYPath metadataTable = NYT::JoinYPaths(dataDir, METADATA_TABLE);
            NYT::TYPath dataTable = NYT::JoinYPaths(dataDir, DATA_TABLE);
            NYT::TYPath statTable = NYT::JoinYPaths(dataDir, STAT_TABLE);
            std::array allTables{metadataTable, dataTable, statTable};
            Y_ASSERT(allTables.size() == ALL_TABLES.size());

            TString metadataSchema{};
            if (options.Version < YT_CACHE_METADATA_SCHEMAS.size()) {
                metadataSchema = YT_CACHE_METADATA_SCHEMAS[options.Version];
            }
            if (metadataSchema.empty()) {
                ythrow TYtStoreError::Muted() << "Incorrect cache version " << options.Version;
            }

            auto ytc = ConnectToCluster(proxy, options.ConnectOptions);

            if (!ytc->Exists(dataDir)) {
                ythrow TYtStoreError::Muted() << "Path '" << dataDir << "' doesn't exist";
            }
            if (!options.IgnoreExisting) {
                for (const NYT::TYPath& table : allTables) {
                    if (ytc->Exists(table)) {
                        ythrow TYtStoreError::Muted() << "Table '" << table << "' already exists";
                    }
                }
            }

            INFO_LOG << "Create tables";
            NYT::ENodeType tableType{};
            auto metadataAttrs = NYT::TNode::CreateMap();
            auto dataAttrs = NYT::TNode::CreateMap();
            if (options.Replicated) {
                tableType = NYT::ENodeType::NT_REPLICATED_TABLE;
            } else {
                tableType = NYT::ENodeType::NT_TABLE;
                auto commonAttrs = NYT::TNode()(
                    "mount_config", NYT::TNode()
                        ("periodic_compaction_mode", "partition")
                );
                metadataAttrs = commonAttrs;
                dataAttrs = commonAttrs;
                if (options.InMemory) {
                    metadataAttrs("in_memory_mode", "uncompressed");
                }
                dataAttrs(
                    "tablet_balancer_config", NYT::TNode()
                        // Don't let the balancer creates too many tablets
                        ("enable_auto_reshard", false)
                        ("enable_auto_tablet_move", true)
                );
            }
            CreateDynamicTable(
                ytc,
                metadataTable,
                tableType,
                metadataSchema,
                options.MetadataTabletCount.value_or(DEFAULT_METADATA_TABLET_COUNT),
                options.IgnoreExisting,
                options.Tracked,
                metadataAttrs
            );
            CreateDynamicTable(
                ytc,
                dataTable,
                tableType,
                YT_CACHE_DATA_SCHEMA,
                options.DataTabletCount.value_or(DEFAULT_DATA_TABLET_COUNT),
                options.IgnoreExisting,
                options.Tracked,
                dataAttrs
            );
            CreateDynamicTable(ytc, statTable, tableType, YT_CACHE_STAT_SCHEMA, 0, options.IgnoreExisting, options.Tracked);

            if (options.Replicated && !ytc->Exists(NYT::JoinYPaths(metadataTable, "@replication_collocation_id"))) {
                auto tablePaths = NYT::TNode::CreateList();
                for (const TYPath& path : allTables) {
                    tablePaths.Add(path);
                }
                auto opts = TCreateOptions().Attributes(
                    NYT::TNode()
                        ("collocation_type", "replication")
                        ("table_paths", tablePaths)
                );
                try {
                    ytc->Create("", NYT::ENodeType::NT_TABLE_COLLOCATION, opts);
                } catch (const yexception& e) {
                    WARNING_LOG << "Cannot create table collocation (not supported on the cluster?): " << e.what();
                }
            }

            if (options.Mount) {
                INFO_LOG << "Mount tables";
                MountTables(ytc, allTables);
            }
        }

        static inline void ModifyTablesState(const TString& proxy, const TString& dataDir, const TModifyTablesStateOptions& options) {
            using EAction = TModifyTablesStateOptions::EAction;

            auto ytc = ConnectToCluster(proxy, options.ConnectOptions);
            TVector<NYT::TYPath> allTables;
            for (const auto& table : ALL_TABLES) {
                allTables.push_back(NYT::JoinYPaths(dataDir, table));
            }
            switch (options.Action) {
                case EAction::MOUNT:
                    MountTables(ytc, allTables);
                    break;
                case EAction::UNMOUNT:
                    UnmountTables(ytc, allTables);
                    break;
                default:
                    Y_UNREACHABLE();
            }
        }

        static inline void ModifyReplica(
            const TString& proxy,
            const TString& dataDir,
            const TString& replicaProxy,
            const TString& replicaDataDir,
            const TModifyReplicaOptions& options
        ) {
            auto ytc_main = ConnectToCluster(proxy, options.ConnectOptions);
            auto ytc_replica = ConnectToCluster(replicaProxy, options.ConnectOptions);

            THashMap<NYT::TYPath, NYT::TYPath> allReplicaTables;
            for (const TYPath& table : ALL_TABLES) {
                NYT::TYPath mainTable = NYT::JoinYPaths(dataDir, table);
                NYT::TYPath replicaTable = NYT::JoinYPaths(replicaDataDir, table);
                allReplicaTables[mainTable] = replicaTable;
            }

            // Get existing replicas
            THashMap<NYT::TYPath, NYT::TReplicaId> existingReplicas{};
            auto getOpts = TGetOptions().AttributeFilter(TAttributeFilter().Attributes({"type", "replicas"}));
            for (const auto& [mainTable, replicaTable] : allReplicaTables) {
                auto mainAttrs = ytc_main->Get(NYT::JoinYPaths(mainTable, "@"), getOpts);
                // Just in case user passes incorrect cluster
                if (mainAttrs.ChildAsString("type") != "replicated_table") {
                    ythrow TYtStoreError::Muted() << "Table '" << mainTable << "' must be a replicated table";
                }

                for (const TRawReplicaInfo& info : ParseReplicasAttr(mainAttrs.At("replicas"))) {
                    if (info.ClusterName == replicaProxy && info.ReplicaPath == replicaTable) {
                        existingReplicas[mainTable] = info.Id;
                        break;
                    }
                }
            }

            if (options.Action == TModifyReplicaOptions::EAction::REMOVE) {
                for (const auto& [mainTable, replicaTable] : allReplicaTables) {
                    if (NYT::TReplicaId* idPtr = existingReplicas.FindPtr(mainTable)) {
                        ytc_main->Remove("#" + GetGuidAsString(*idPtr));
                        INFO_LOG << "Replica " << proxy << ":" << mainTable << " -> " << replicaProxy << ":" << replicaTable << " is removed";
                    } else {
                        WARNING_LOG << "Replica " << proxy << ":" << mainTable << " -> " << replicaProxy << ":" << replicaTable << " doesn't exist";
                    }
                }
                return;
            }

            Y_ENSURE_FATAL(options.Action == TModifyReplicaOptions::EAction::CREATE, "Unexpected action: " + ToString(int(options.Action)));

            bool alterReplica = false;
            TAlterTableReplicaOptions alterTableReplicaOptions{};
            if (options.Enable.has_value()) {
                alterTableReplicaOptions.Enabled(options.Enable.value());
                alterReplica = true;
            }
            if (options.SyncMode.has_value()) {
                alterTableReplicaOptions.Mode(options.SyncMode.value() ? NYT::ETableReplicaMode::Sync : NYT::ETableReplicaMode::Async);
                alterReplica = true;
            }
            for (const auto& [mainTable, replicaTable] : allReplicaTables) {
                NYT::TReplicaId replicaId;
                if (NYT::TReplicaId* idPtr = existingReplicas.FindPtr(mainTable)) {
                    DEBUG_LOG << "Replica " << proxy << ":" << mainTable << " -> " << replicaProxy << ":" << replicaTable << " already exists";
                    replicaId = *idPtr;
                } else {
                    auto replAttrs = NYT::TNode()
                        ("table_path", mainTable)
                        ("cluster_name", replicaProxy)
                        ("replica_path", replicaTable);
                    replicaId = ytc_main->Create("", NYT::NT_TABLE_REPLICA, NYT::TCreateOptions().Attributes(replAttrs));
                    ytc_replica->AlterTable(replicaTable, TAlterTableOptions().UpstreamReplicaId(replicaId));
                    INFO_LOG << "Replica " << proxy << ":" << mainTable << " -> " << replicaProxy << ":" << replicaTable << " is created";
                }
                if (alterReplica) {
                    INFO_LOG << "Replica " << proxy << ":" << mainTable << " -> " << replicaProxy << ":" << replicaTable << " is altered";
                    ytc_main->AlterTableReplica(replicaId, alterTableReplicaOptions);
                }
            }
        }

        std::unique_ptr<TInternalState> GetInternalState() {
            auto config = InitializeControl_.GetValue();

            auto state = std::make_unique<TInternalState>();
            state->Atomicity = config->Atomicity;
            state->Version = config->Version;
            for (const auto& replica : config->Replicas) {
                state->GoodReplicas.push_back({
                    .Proxy = replica.Proxy,
                    .DataDir = replica.DataDir,
                    .Lag = replica.Lag,
                });
            }
            if (PrepareControl_.Future.Initialized()) {
                const auto& prepareResult = PrepareControl_.GetValue();
                state->PreparedReplica.Proxy = prepareResult->Task->Proxy;
                state->PreparedReplica.DataDir = prepareResult->Task->DataDir;
                state->PreparedReplica.Lag = prepareResult->Task->Lag;
            }
            return std::move(state);
        }

    private:
        static inline const NYT::TYPath TMP_DIR = "//tmp";
        static inline const NYT::TYPath METADATA_TABLE = "metadata";
        static inline const NYT::TYPath DATA_TABLE = "data";
        static inline const NYT::TYPath STAT_TABLE = "stat";
        static inline const std::array ALL_TABLES = {METADATA_TABLE, DATA_TABLE, STAT_TABLE};
        static inline const size_t REMOVE_BATCH_SIZE = 10'000;
        static constexpr size_t REPLICATION_FACTOR = 3;
        static constexpr TDuration RETRY_MIN_DELAY = TDuration::Seconds(0.1);
        static constexpr double RETRY_SCALE_FACTOR = 1.3;
        static constexpr TDuration RETRY_MAX_DELAY = TDuration::Seconds(10);
        static constexpr size_t RETRY_MAX_COUNT = 5;
        static constexpr TDuration DATA_GC_MIN_AGE = TDuration::Hours(2);
        static constexpr ui64 DEFAULT_METADATA_TABLET_COUNT = 128;
        static constexpr ui64 DEFAULT_DATA_TABLET_COUNT = 256;
        static constexpr TDuration DEFAULT_INTERACTIVE_INIT_TIMEOUT = TDuration::Seconds(5);
        static constexpr TDuration DEFAULT_INTERACTIVE_PREPARE_TIMEOUT = TDuration::Seconds(8); // XXX YA-2886
        static constexpr TDuration DEFAULT_BG_INIT_TIMEOUT = TDuration::Seconds(30);
        static constexpr TDuration DEFAULT_BG_PREPARE_TIMEOUT = TDuration::Seconds(30);
        static constexpr TDuration MAX_APPROPRIATE_REPLICA_LAG = TDuration::Seconds(60);
        static inline const THashSet<TString> NOT_LOADED_META_COLUMNS = {"tablet_hash", "hostname", "GSID", "create_time"};
        static constexpr TDuration METADATA_REFRESH_AGE_THRESHOLD_SEC = TDuration::Seconds(300);
        static constexpr size_t INSERT_ROWS_LIMIT = 5000;  // Too high value increases transaction conflicts
        static constexpr TDuration DATA_LOOKUP_TIMEOUT = TDuration::Seconds(60);
        static inline const TString NO_DATA_CODEC = "no_data";
        static constexpr ui64 DEFAULT_MAX_MEMORY_USAGE = 8_GB;
        static constexpr ui64 YT_CACHE_CELL_LIMIT = 14_MB;
        static constexpr int YT_CELLS_PER_INSERT_LIMIT = 4;

        using TRetryPolicy = IRetryPolicy<const yexception&>;
        using TRetryPolicyPtr = TRetryPolicy::TPtr;
        using TOnFailFunc = std::function<void(const yexception&)>;

        struct TMainCluster {
            TString Proxy;
            NYT::TYPath DataDir;
            NYT::IClientPtr Client;
        };

        struct TReplica {
            TString Proxy;
            NYT::TYPath DataDir;
            NYT::IClientPtr Client;
            bool Sync;
            TDuration Lag;
        };

        struct TKeyRange {
            ui64 Begin;
            std::optional<ui64> End;

            friend IOutputStream& operator<<(IOutputStream& stream, const TKeyRange& self) {
                stream << self.Begin << "-";
                if (self.End) {
                    stream << *self.End;
                } else {
                    stream << "unlimited";
                }
                return stream;
            }

            NYT::TReadRange AsReadRange() const {
                auto range = NYT::TReadRange().LowerLimit(NYT::TReadLimit().Key(Begin));
                if (End) {
                    range.UpperLimit(NYT::TReadLimit().Key(*End));
                }
                return range;
            }
        };

        struct TRawReplicaInfo {
            TGUID Id;
            TString ClusterName{};
            NYT::TYPath ReplicaPath{};
            TString State{};
            TString Mode{};
            TDuration Lag{};

            TYPath GetDataDir() const {
                return TYPath{TStringBuf{ReplicaPath}.RSplitOff('/')};
            }

            bool Enabled() const {
                return State == "enabled";
            }

            bool Sync() const {
                return Mode == "sync";
            }
        };

        struct TConfigureResult : public TThrRefBase {
            TMainCluster MainCluster{};
            TVector<TReplica> Replicas{};
            bool Replicated{};
            TVector<TString> MetadataColumns{};
            TVector<TString> MetadataKeyColumns{};
            unsigned Version{};
            NYT::EAtomicity Atomicity{NYT::EAtomicity::Full};
            bool RequireSyncReplica{};
        };
        using TConfigureResultPtr = TIntrusivePtr<TConfigureResult>;

        struct TInitializeControl {
            NThreading::TPromise<TConfigureResultPtr> Init(TDuration timeout) {
                Y_ENSURE_FATAL(!Future.Initialized(), "Initialization has already started");
                auto promise = NThreading::NewPromise<TConfigureResultPtr>();
                Future = promise.GetFuture();
                DeadLine = timeout.ToDeadLine();
                return promise;
            }

            TConfigureResultPtr GetValue() {
                Y_ENSURE_FATAL(Future.Initialized(), "Initialization should start before getting value");
                if (!Future.Wait(DeadLine)) {
                    ythrow TYtStoreInitTimeoutError() << "Initialization timed out";
                }
                return Future.GetValueSync();
            }

            NThreading::TFuture<TConfigureResultPtr> Future{};
            TInstant DeadLine{};
        };

        struct TLoadMetaTask : public TThrRefBase {
            TLoadMetaTask(
                TString proxy,
                NYT::TYPath dataDir,
                NYT::IClientPtr client,
                TDuration lag
            )
                : Proxy{proxy}
                , DataDir{dataDir}
                , Client{client}
                , Lag{lag}
            {
            }

            TString Proxy;
            NYT::TYPath DataDir;
            NYT::IClientPtr Client;
            TDuration Lag;
        };
        using TLoadMetaTaskPtr = TIntrusivePtr<TLoadMetaTask>;

        using TMetaData = THashMap<TString, NYT::TNode>;
        struct TPrepareResult : public TThrRefBase {
            template <class T>
            TPrepareResult(T&& meta, TLoadMetaTaskPtr task)
                : Meta{std::forward<T>(meta)}
                , Task{task}
            {
            }
            TMetaData Meta;
            TLoadMetaTaskPtr Task;
        };
        using TPrepareResultPtr = TIntrusivePtr<TPrepareResult>;

        struct TPrepareControl {
            NThreading::TPromise<TPrepareResultPtr> Init() {
                Y_ENSURE_FATAL(!Future.Initialized(), "PrepareControl is already initialized (prepare is called twice?)");
                auto promise = NThreading::NewPromise<TPrepareResultPtr>();
                Future = promise.GetFuture();
                return promise;
            }

            TPrepareResultPtr GetValue() {
                Y_ENSURE_FATAL(Future.Initialized(), "PrepareControl is not initialized (forget to call prepare?)");
                NeedResult.Cancel();
                return Future.GetValueSync();
            }

            NThreading::TFuture<TPrepareResultPtr> Future{};
            NThreading::TCancellationTokenSource NeedResult{};
        };

        class TMetricsManager {
        public:
            void IncTime(const TString tag, TInstant start, TInstant stop) {
                with_lock (Lock_) {
                    Data_.Timers[tag] += stop - start;
                    Data_.TimerIntervals[tag].emplace_back(start, stop);
                    ++Data_.Counters[tag];
                };
            }

            void IncCounter(const TString tag) {
                with_lock (Lock_) {
                    ++Data_.Counters[tag];
                }
            }

            void CountFailures(const TString& tag) {
                with_lock (Lock_) {
                    ++Data_.Failures[tag];
                };
            }

            void IncDataSize(const TString& tag, size_t value) {
                with_lock (Lock_) {
                    Data_.DataSize[tag] += value;
                };
            }

            void SetCacheHit(size_t requested, size_t found) {
                with_lock (Lock_) {
                    Data_.Requested = requested;
                    Data_.Found = found;
                }
            }

            void UpdateCompressionRatio(size_t compressed, size_t raw) {
                with_lock (Lock_) {
                    Data_.TotalCompressedSize += compressed;
                    Data_.TotalRawSize += raw;
                };
            }

            void SetTimeToFirstCallHas() {
                with_lock (Lock_) {
                    if (Y_UNLIKELY(!Data_.TimeToFirstCallHas)) {
                        Data_.TimeToFirstCallHas = TInstant::Now();
                    }
                }
            }

            void SetTimeToFirstRecvMeta() {
                with_lock (Lock_) {
                    if (Y_UNLIKELY(!Data_.TimeToFirstRecvMeta)) {
                        Data_.TimeToFirstRecvMeta = TInstant::Now();
                    }
                }
            }

            TMetrics GetData() const {
                with_lock (Lock_) {
                    return Data_;
                }
            }

        private:
            TMetrics Data_{};
            mutable TAdaptiveLock Lock_{};
        };

    private:
        template <class TResult>
        auto WithRetry(std::function<TResult()> func, TOnFailFunc onFail = {}) -> decltype(func()) {
            return std::move(*DoWithRetry(func, RetryPolicy_, true, onFail));
        }

        template <>
        void WithRetry(std::function<void()> func, TOnFailFunc onFail) {
            DoWithRetry(func, RetryPolicy_, true, onFail);
        }

        template <class TFunc>
        auto RetryYtError(TFunc func) -> decltype(func()) {
            return WithRetry(std::function(func));
        }

        template <class TFunc>
        auto RetryYtErrorUntilCancelled(NThreading::TCancellationToken cToken, TFunc func) ->decltype(func()) {
            return WithRetry(std::function(func), [cToken](const yexception&) {
                cToken.ThrowIfTokenCancelled();
            });
        }

        template <class TFunc, class T = std::invoke_result<TFunc()>>
        T CheckDisabledAndDisableOnError(T defaultValue, TFunc&& func) {
            if (!Disabled()) {
                try {
                    return func();
                } catch (const TYtStoreFatalError&) {
                    throw;
                } catch (...) {
                    Disable(CritLevel_ != ECritLevel::NONE);
                }
            }
            return defaultValue;
        }

        template <class TFunc>
        void CheckDisabledAndDisableOnError(TFunc&& func) {
            if (!Disabled()) {
                try {
                    func();
                } catch (const TYtStoreFatalError&) {
                    throw;
                } catch (...) {
                    Disable(CritLevel_ != ECritLevel::NONE);
                }
            }
        }

        NYT::IClientPtr ConnectToCluster(const TString& proxy, bool defaultRetryPolicy = false) {
            if (defaultRetryPolicy) {
                return ConnectToCluster(proxy, ConnectOptions_);
            }
            auto options = TCreateClientOptions();
            NYT::TConfigPtr cfg = MakeIntrusive<NYT::TConfig>();
            cfg->ConnectTimeout = TDuration::Seconds(5);
            cfg->SocketTimeout = TDuration::Seconds(5);
            cfg->RetryCount = 1;
            options.Config(cfg);
            return ConnectToCluster(proxy, ConnectOptions_, std::move(options));
        }

        static inline NYT::IClientPtr ConnectToCluster(const TString& proxy, const TYtConnectOptions& connectOptions, NYT::TCreateClientOptions options = {}) {
            if (connectOptions.Token) {
                options.Token(connectOptions.Token);
            }
            if (connectOptions.ProxyRole) {
                if (!options.Config_) {
                    options.Config(MakeIntrusive<NYT::TConfig>());
                }
                options.Config_->HttpProxyRole = connectOptions.ProxyRole;
            }
            if (YtClusterConnectorPtr) {
                return (*YtClusterConnectorPtr)(proxy, options);
            }
            return CreateClient(proxy, options);
        }

        static inline TVector<TRawReplicaInfo> ParseReplicasAttr(const NYT::TNode& replicasAttr) {
            TVector<TRawReplicaInfo> replicas{};
            for (const auto& [id, replicaAttr] : replicasAttr.AsMap()) {
                TRawReplicaInfo replica;
                replica.Id = GetGuid(id);
                replica.ClusterName = replicaAttr.ChildAsString("cluster_name");
                replica.ReplicaPath = replicaAttr.ChildAsString("replica_path");
                replica.State = replicaAttr.ChildAsString("state");
                replica.Mode = replicaAttr.ChildAsString("mode");
                replica.Lag = TDuration::MilliSeconds(replicaAttr.ChildAsInt64("replication_lag_time"));
                replicas.push_back(std::move(replica));
            }
            return replicas;
        }

        template <class TOpts>
        requires (std::is_base_of_v<TTabletTransactionOptions<TOpts>, TOpts>)
        TOpts MakeTransactionOpts(TConfigureResultPtr config) {
            return TOpts().Atomicity(config->Atomicity).RequireSyncReplica(config->RequireSyncReplica);
        }

        template <class TSpec>
        void AddPool(TSpec& spec) {
            if (OperationPool_) {
                spec.Pool(OperationPool_);
            }
        }

        std::exception_ptr Disable(bool rethrow = false) {
            auto errPtr = std::current_exception();
            if (!Disabled_.exchange(true, std::memory_order::relaxed)) {
                TString errType{};
                TString errMessage{};
                try {
                    std::rethrow_exception(errPtr);
                } catch (const std::exception& e) {
                    if (IsYtAuthError(e)) {
                        errType = "YtAuthError";
                    } else {
                        errType = TypeName(e);
                    }
                    errMessage = e.what();
                } catch (...) {
                    errType = "UNKNOWN";
                    errMessage = "Unknown exception (not a std::exception descendant)";
                }
                if (Owner_) {
                    CallHook(YaYtStoreDisableHook, Owner_, errType, errMessage);
                }
                // For any other CritLevel value, the exception is escalated and results in program termination, so the log message is redundant
                if (CritLevel_ == ECritLevel::NONE) {
                    TString messagePrefix{"Disabling dist cache. Last caught error: "};
                    if (errMessage.size() > 100) {
                        WARNING_LOG << messagePrefix << errMessage.substr(0, 100) << "...<Truncated. Complete message will be available in debug logs>";
                        DEBUG_LOG << messagePrefix << errMessage;
                    } else {
                        WARNING_LOG << messagePrefix << errMessage;
                    }
                }
            }
            if (rethrow) {
                std::rethrow_exception(errPtr);
            }
            return errPtr;
        }

        TConfigureResultPtr Configure(const TString& proxy, const TYPath& dataDir) {
            TConfigureResultPtr config = MakeIntrusive<TConfigureResult>();

            config->MainCluster.Proxy = proxy;
            config->MainCluster.DataDir = dataDir;
            config->MainCluster.Client = ConnectToCluster(proxy);

            NYT::TNode metadataAttrs = RetryYtError([&]() {
                auto getOpts = NYT::TGetOptions()
                    .AttributeFilter(TAttributeFilter().Attributes({"type", "schema", "atomicity", "replicas"}))
                    .ReadFrom(EMasterReadKind::Cache);
                return config->MainCluster.Client->Get(NYT::JoinYPaths(config->MainCluster.DataDir, METADATA_TABLE, "@"), getOpts);
            });
            auto metadataSchema = metadataAttrs.At("schema");
            config->Atomicity = metadataAttrs.At("atomicity") == "none" ? NYT::EAtomicity::None : NYT::EAtomicity::Full;

            for (const NYT::TNode& column : metadataSchema.AsList()) {
                if (!column.HasKey("expression")) {
                    config->MetadataColumns.push_back(column.ChildAsString("name"));
                    if (column.HasKey("sort_order")) {
                        config->MetadataKeyColumns.push_back(column.ChildAsString("name"));
                    }
                }
            }
            if (Count(config->MetadataColumns, "self_uid")) {
                config->Version = 3;
            } else if (Count(config->MetadataColumns, "access_time")) {
                config->Version = 2;
            } else {
                ythrow TYtStoreError() << "Unsupported metadata table schema";
            }
            DEBUG_LOG << "Cache version = " << config->Version;

            if (metadataAttrs.At("type") == "replicated_table") {
                config->Replicated = true;

                // Read data table replicas
                NYT::TNode dataRawReplicas = RetryYtError([&]() {
                    auto getOpts = NYT::TGetOptions()
                        .ReadFrom(EMasterReadKind::Cache);
                    return config->MainCluster.Client->Get(NYT::JoinYPaths(config->MainCluster.DataDir, DATA_TABLE, "@replicas"), getOpts);
                });
                THashMap<std::pair<TString, NYT::TYPath>, TRawReplicaInfo> dataReplicas;
                for (const TRawReplicaInfo& replicaInfo : ParseReplicasAttr(dataRawReplicas)) {
                    dataReplicas[std::make_pair(replicaInfo.ClusterName, replicaInfo.GetDataDir())] = replicaInfo;
                }

                for (const TRawReplicaInfo& metadataReplicaInfo : ParseReplicasAttr(metadataAttrs.At("replicas"))) {
                    const TRawReplicaInfo* dataReplicaInfoPtr = dataReplicas.FindPtr(std::make_pair(metadataReplicaInfo.ClusterName, metadataReplicaInfo.GetDataDir()));
                    if (!dataReplicaInfoPtr) {
                        TString error = "Inconsistent cache configuration: data table replica not found for "
                            + metadataReplicaInfo.ClusterName + ":" + metadataReplicaInfo.GetDataDir();
                        if (ReadOnly_) {
                            WARNING_LOG << error;
                            continue;
                        } else {
                            ythrow TYtStoreError::Muted() << error;
                        }
                    }

                    // In the readonly mode we use the lag to get best replica so sync/async mode is not so important as for writers
                    if (!ReadOnly_ && metadataReplicaInfo.Sync() != dataReplicaInfoPtr->Sync()) {
                        ythrow TYtStoreError::Muted() << "Inconsistent cache configuration: metadata and data tables have a different replication mode for "
                            << metadataReplicaInfo.ClusterName << ":" << metadataReplicaInfo.GetDataDir();
                    }

                    bool isGood = metadataReplicaInfo.Enabled() && dataReplicaInfoPtr->Enabled();
                    TDuration lag = Max(metadataReplicaInfo.Lag, dataReplicaInfoPtr->Lag);

                    if (metadataReplicaInfo.Sync()) {
                        // if we have at least one sync replica force to use it
                        config->RequireSyncReplica = true;
                    }

                    DEBUG_LOG << (isGood ? "Use" : "Ignore disabled") << " replica: " <<
                        "proxy=" << metadataReplicaInfo.ClusterName <<
                        ", dir=" << metadataReplicaInfo.GetDataDir() <<
                        ", state=" << metadataReplicaInfo.State <<
                        ", mode=" <<  metadataReplicaInfo.Mode <<
                        ", lag=" << lag;

                    if (isGood) {
                        config->Replicas.push_back({
                            .Proxy = metadataReplicaInfo.ClusterName,
                            .DataDir = metadataReplicaInfo.GetDataDir(),
                            .Client = ConnectToCluster(metadataReplicaInfo.ClusterName),
                            .Sync = metadataReplicaInfo.Sync(),
                            .Lag = lag
                        });
                    }
                }
                if (config->Replicas.empty()) {
                    ythrow TYtStoreError::Muted() << "No enabled replica is found";
                }
            }
            return config;
        }

        void Check(TConfigureResultPtr config) {
            if (!config->Replicated && CritLevel_ != ECritLevel::NONE || config->RequireSyncReplica && CritLevel_ == ECritLevel::PUT) {
                // Check important tables availability
                for (const NYT::TYPath& table : {METADATA_TABLE, DATA_TABLE}) {
                    TString query = "1 from [" + NYT::JoinYPaths(config->MainCluster.DataDir, table) + "] limit 1";
                    RetryYtError([&] {
                        config->MainCluster.Client->SelectRows(query);
                    });
                }
            }

            // Check that the cache is not full.
            // For replicated cache tables size is got from the most fresh (best) replica
            if (!ReadOnly_ && CheckSize_) {
                NYT::IClientPtr client{};
                NYT::TYPath dataDir{};
                if (config->Replicated) {
                    TReplica& bestReplica = GetBestReplica(config);
                    client = bestReplica.Client;
                    dataDir = bestReplica.DataDir;
                } else {
                    client = config->MainCluster.Client;
                    dataDir = config->MainCluster.DataDir;
                }
                if (size_t maxSize = GetMaxCacheSize(client, dataDir)) {
                    size_t currentSize = GetTablesSize(client, dataDir);
                    if (maxSize < currentSize) {
                        if (CritLevel_ == ECritLevel::PUT) {
                            ythrow TYtStoreError::Muted() << "Cache size (" << currentSize << ") exceeds limit of " << maxSize;
                        } else {
                            WARNING_LOG << "Cache size (" << currentSize << ") exceeds limit of " << maxSize << " bytes, switch to readonly mode";
                            ReadOnly_ = true;
                        }
                    }
                }
            }
        }

        void Initialize(NThreading::TPromise<TConfigureResultPtr>&& promise, const TString& proxy, const TYPath& dataDir) {
            StartStage("init-yt-store");
            Y_DEFER {
                StopStage("init-yt-store");
            };
            try {
                TConfigureResultPtr config = Configure(proxy, dataDir);
                Check(config);
                promise.SetValue(config);
            } catch (...) {
                promise.SetException(Disable());
            }
        }

        TReplica& GetBestReplica(TConfigureResultPtr config) {
            Y_ENSURE_FATAL(config->Replicated, "Not a replicated cache");
            TDuration minLag = TDuration::Max();
            TReplica* bestReplica{};
            for (TReplica& r : config->Replicas) {
                if (r.Sync) {
                    return r;
                }
                if (r.Lag < minLag) {
                    minLag = r.Lag;
                    bestReplica = &r;
                }
            }
            return *bestReplica;
        }

        std::pair<IClientPtr, TYPath> GetOpCluster(TConfigureResultPtr config) {
            if (config->Replicated) {
                const TReplica& bestReplica = GetBestReplica(config);
                DEBUG_LOG << "Operation replica: " << bestReplica.Proxy << ":" << bestReplica.DataDir;
                // Note: for operations we use separate client with default retry policy
                return std::make_pair(ConnectToCluster(bestReplica.Proxy, true), bestReplica.DataDir);
            } else {
                // Note: for operations we use separate client with default retry policy
                return std::make_pair(ConnectToCluster(config->MainCluster.Proxy, true), config->MainCluster.DataDir);
            }
        }

        size_t GetQuotaSize(IClientPtr client, const TYPath& dataDir) {
            NYT::TNode tableAttrs = RetryYtError([&]{
                auto getOpts = NYT::TGetOptions()
                    .AttributeFilter(TAttributeFilter().Attributes({"primary_medium", "account"}))
                    .ReadFrom(EMasterReadKind::Cache);
                return client->Get(NYT::JoinYPaths(dataDir, DATA_TABLE, "@"), getOpts);
            });
            const TString& accountName = tableAttrs.ChildAsString("account");
            const TString& primaryMedium = tableAttrs.ChildAsString("primary_medium");
            const NYT::TNode limits = RetryYtError([&] {
                return client->Get(
                    NYT::JoinYPaths("//sys/accounts/", accountName, "@resource_limits/disk_space_per_medium"),
                    NYT::TGetOptions().ReadFrom(EMasterReadKind::Cache)
                );
            });
            return limits.ChildAsInt64(primaryMedium);
        }

        size_t GetMaxCacheSize(IClientPtr client, const TYPath& dataDir) {
            if (std::holds_alternative<double>(MaxCacheSize_)) {
                auto perc = std::get<double>(MaxCacheSize_);
                Y_ENSURE(perc > 0 && perc <= 100, "Wrong max cache size value");
                size_t quotaSize = GetQuotaSize(client, dataDir);
                MaxCacheSize_ = static_cast<size_t>(quotaSize * perc / 100);
            }
            return std::get<size_t>(MaxCacheSize_);
        }

        size_t GetTablesSize(IClientPtr client, const TYPath& dataDir) {
            size_t total = 0;
            for (const TYPath& tableName : {METADATA_TABLE, DATA_TABLE}) {
                const NYT::TNode space = RetryYtError([&] {
                    return client->Get(
                        NYT::JoinYPaths(dataDir, tableName, "@resource_usage", "disk_space"),
                        TGetOptions().ReadFrom(EMasterReadKind::Cache)
                    );
                });
                total += space.AsInt64();
            }
            return total;
        }

        void StartStage(const TString& name) {
            if (Owner_) {
                CallHook(YaYtStoreStartStage, Owner_, name);
            }
        }

        void StopStage(const TString& name) {
            if (Owner_) {
                CallHook(YaYtStoreFinishStage, Owner_, name);
            }
        }

        void DoPrepare(NThreading::TPromise<TPrepareResultPtr>&& promise, TPrepareOptionsPtr options, TInstant deadLine) {
            try {
                auto config = InitializeControl_.GetValue();

                auto startTime = TInstant::Now();
                Y_DEFER {
                    Metrics_.IncTime("get-meta", startTime, TInstant::Now());
                };

                NThreading::TCancellationTokenSource loadMetaCancellation{};
                StartStage("loading-yt-meta");
                Y_DEFER {
                    loadMetaCancellation.Cancel();
                    StopStage("loading-yt-meta");
                };
                TVector<TLoadMetaTaskPtr> tasks;
                if (config->Replicated) {
                    bool syncReplicaOnly = !ReadOnly_ && CritLevel_ == ECritLevel::PUT && config->RequireSyncReplica;
                    for (TReplica& replica : config->Replicas) {
                        if (syncReplicaOnly && !replica.Sync) {
                            continue;
                        }
                        tasks.push_back(MakeIntrusive<TLoadMetaTask>(replica.Proxy, replica.DataDir, replica.Client, replica.Lag));
                    }
                } else {
                    tasks.push_back(MakeIntrusive<TLoadMetaTask>(config->MainCluster.Proxy, config->MainCluster.DataDir, config->MainCluster.Client, TDuration::Zero()));
                }
                Y_ENSURE(!tasks.empty());

                std::list<NThreading::TFuture<TPrepareResultPtr>> futures{};
                auto metaCancellationToken = loadMetaCancellation.Token();
                for (auto task : tasks) {
                    auto loadMetaPromise = NThreading::NewPromise<TPrepareResultPtr>();
                    futures.push_back(loadMetaPromise.GetFuture());
                    ThreadPool_.SafeAddFunc([this, config, promise=std::move(loadMetaPromise), task, options, metaCancellationToken]() mutable {
                        TThread::SetCurrentThreadName("YtStore::LoadMeta");
                        try {
                            DEBUG_LOG << "Start load meta from " << task->Proxy << ":" << task->DataDir;
                            auto meta = LoadMetadata(config, task, options, metaCancellationToken);
                            promise.SetValue(MakeIntrusive<TPrepareResult>(std::move(meta), task));
                        } catch (const NThreading::TOperationCancelledException&) {
                            DEBUG_LOG << "Cancel load meta from " << task->Proxy << ":" << task->DataDir;
                        } catch (...) {
                            DEBUG_LOG << "Load metadata from " << task->Proxy << ":" << task->DataDir << " failed with error: " << CurrentExceptionMessage();
                            promise.SetException(std::current_exception());
                        }
                    });
                }

                TPrepareResultPtr currentResult{};
                auto needResultToken = PrepareControl_.NeedResult.Token();
                auto needResultFuture = needResultToken.Future();
                std::exception_ptr firstError;
                // NOTE:
                // If there are good replicas, we will wait until we get a result from any of them or the preparation timeout expires.
                // If there is no good replica, we treat the first result as an appropriate one.
                bool goodReplicaExists = AnyOf(tasks, [](const auto& t) {return t->Lag < MAX_APPROPRIATE_REPLICA_LAG;});
                bool appropriateResultReceived = false;
                while (!futures.empty()) {
                    auto payloadFuture = NThreading::NWait::WaitAny(futures);
                    auto groupFuture = appropriateResultReceived ? NThreading::NWait::WaitAny(payloadFuture, needResultFuture) : payloadFuture;
                    if (!groupFuture.Wait(deadLine)) {
                        if (!currentResult) {
                            ythrow TYtStorePrepareTimeoutError() << "Prepare timed out";
                        }
                        break;
                    }
                    if (payloadFuture.HasValue() || payloadFuture.HasException()) {
                        for (auto it = futures.begin(); it != futures.end();) {
                            if (it->HasValue()) {
                                auto newResult = it->ExtractValueSync();
                                if (newResult && (!currentResult || currentResult->Task->Lag > newResult->Task->Lag)) {
                                    currentResult = newResult;
                                    appropriateResultReceived = !goodReplicaExists || newResult->Task->Lag < MAX_APPROPRIATE_REPLICA_LAG;
                                }
                                it = futures.erase(it);
                            } else if (it->HasException()) {
                                // Catch any error to report to user if all replicas fail
                                if (!firstError) {
                                    try {
                                        it->TryRethrow();
                                    } catch (...) {
                                        firstError = std::current_exception();
                                    }
                                }
                                it = futures.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    if (appropriateResultReceived && needResultToken.IsCancellationRequested()) {
                        break;
                    }
                }
                loadMetaCancellation.Cancel();

                if (!currentResult) {
                    std::rethrow_exception(firstError);
                }

                THashSet<TString> UidSet{options->Uids.begin(), options->Uids.end()};
                Metrics_.SetCacheHit(
                    options->Uids.size(),
                    CountIf(options->Uids, [&](const TString& uid) {return currentResult->Meta.contains(uid);})
                );

                DEBUG_LOG << "Use metadata from " << currentResult->Task->Proxy << ":" << currentResult->Task->DataDir;

                if (options->RefreshOnRead) {
                    RefreshAccessTime(config, currentResult->Meta, options->Uids);
                }
                Metrics_.SetTimeToFirstRecvMeta();

                promise.SetValue(currentResult);
            } catch (...) {
                promise.SetException(Disable());
            }
        }

        TMetaData LoadMetadata(TConfigureResultPtr config, TLoadMetaTaskPtr task, TPrepareOptionsPtr options, NThreading::TCancellationToken cancellationToken) {
            TVector<TString> loadedColumns{};
            for (const TString& col : config->MetadataColumns) {
                if (!NOT_LOADED_META_COLUMNS.contains(col)) {
                    loadedColumns.push_back(col);
                }
            }
            NYT::TNode::TListType rows{};
            auto startTime = TInstant::Now();
            if (config->Version >= 3 && options->ContentUidsEnabled) {
                Y_ENSURE_FATAL(!options->SelfUids.empty(), "SelfUids list must not be empty");
                TString query{};
                TStringOutput queryOut{query};
                queryOut << JoinSeq(",", loadedColumns) << " from [" << NYT::JoinYPaths(task->DataDir, METADATA_TABLE) << "]";
                queryOut << " where self_uid in (";
                for (auto it = options->SelfUids.begin(); it != options->SelfUids.end(); ++it) {
                    if (it != options->SelfUids.begin()) {
                        queryOut << ',';
                    }
                    queryOut << '"' << *it << '"';
                }
                queryOut << ')';
                queryOut.Finish();
                rows = RetryYtErrorUntilCancelled(cancellationToken, [&] {
                    return task->Client->SelectRows(query, TSelectRowsOptions().InputRowLimit(Max<i64>()).OutputRowLimit(Max<i64>()));
                });
            } else {
                Y_ENSURE_FATAL(options->SelfUids.size() == options->Uids.size(), "SelfUids and Uids lists must be the same size");
                NYT::TNode::TListType keys{};
                if (config->Version >= 3) {
                    for (size_t i = 0; i < options->SelfUids.size(); ++i) {
                        keys.push_back(NYT::TNode()("self_uid", options->SelfUids[i])("uid", options->Uids[i]));
                    }
                } else {
                    for (const TString& uid : options->Uids) {
                        keys.push_back(NYT::TNode()("uid", uid));
                    }
                }
                auto opts = TLookupRowsOptions().Columns(loadedColumns);
                rows = RetryYtErrorUntilCancelled(cancellationToken, [&] {
                    return task->Client->LookupRows(NYT::JoinYPaths(task->DataDir, METADATA_TABLE), keys, opts);
                });
            }
            cancellationToken.ThrowIfCancellationRequested();

            DEBUG_LOG << "Fetched " << rows.size() << " metadata rows from "<< task->Proxy << ":" << task->DataDir << " in " << (TInstant::Now() - startTime);
            TMetaData result{};
            for (auto& row : rows) {
                if (const NYT::TNode* cuidPtr = row.AsMap().FindPtr("cuid")) {
                    if (cuidPtr->IsString() && !cuidPtr->AsString().empty()) {
                        result.emplace(cuidPtr->AsString(), row);
                    }
                }
                result.emplace(row.ChildAsString("uid"), std::move(row));
            }
            return result;
        }

        void RefreshAccessTime(TConfigureResultPtr config, const TMetaData& meta, const TUidList& uids) {
            auto curTime = TInstant::Now();
            TYtTimestamp curTimestamp = ToYtTimestamp(curTime);
            TYtTimestamp thresholdTimestamp = ToYtTimestamp(curTime - METADATA_REFRESH_AGE_THRESHOLD_SEC);

            size_t initialRowCount = 0;
            NYT::TNode::TListType rows;
            THashSet<TString> uidsSet{uids.begin(), uids.end()};
            for (const auto& [uid, row] : meta) {
                // Get only rows which are matched with current graph uids (skip all selected by self_uid)
                if (uidsSet.contains(uid)) {
                    ++initialRowCount;
                    if (row.ChildAs<TYtTimestamp>("access_time") < thresholdTimestamp) {
                        auto newRow = NYT::TNode::CreateMap();
                        for (const auto& c : config->MetadataKeyColumns) {
                            newRow(c, row[c]);
                        }
                        rows.push_back(std::move(newRow("access_time", curTimestamp)));
                    }
                }
            }

            size_t updatedRowCount = 0;
            size_t conflictCount = 0;

            NYT::TYPath metadataTablePath = NYT::JoinYPaths(config->MainCluster.DataDir, METADATA_TABLE);
            auto insertOpts = MakeTransactionOpts<NYT::TInsertRowsOptions>(config).Update(true);
            TVector<TString> columns{config->MetadataKeyColumns.begin(), config->MetadataKeyColumns.end()};
            columns.push_back("access_time");
            auto lookupOpts = NYT::TLookupRowsOptions().Columns(columns);

            for (auto batch_start = rows.begin(); batch_start != rows.end(); ) {
                auto batch_end = batch_start + std::min((std::ptrdiff_t)INSERT_ROWS_LIMIT, rows.end() - batch_start);
                TVector<NYT::TNode> batch{};
                batch.reserve(batch_end - batch_start);
                batch.insert(batch.end(), std::make_move_iterator(batch_start), std::make_move_iterator(batch_end));
                batch_start = batch_end;

                while (!batch.empty()) {
                    bool transactionConflict = RetryYtError([&] {
                        try {
                            config->MainCluster.Client->InsertRows(metadataTablePath, batch, insertOpts);
                            return false;
                        } catch (const NYT::TErrorResponse& e) {
                            if (e.GetError().ContainsErrorCode(NYT::NClusterErrorCodes::NTabletClient::TransactionLockConflict)) {
                                return true;
                            }
                            throw;
                        }
                    });

                    if (!transactionConflict) {
                        updatedRowCount += batch.size();
                        break;
                    }

                    ++conflictCount;
                    SleepAfterTransactionConflict();

                    for (auto& row : batch) {
                        row.AsMap().erase("access_time");
                    }
                    NYT::TNode::TListType actualRows = RetryYtError([&] {
                        return config->MainCluster.Client->LookupRows(metadataTablePath, batch, lookupOpts);
                    });

                    batch.clear();
                    for (auto& row : actualRows) {
                        if (row.ChildAs<TYtTimestamp>("access_time") < thresholdTimestamp) {
                            batch.push_back(std::move(row("access_time", curTimestamp)));
                        }
                    }
                }
            }

            DEBUG_LOG << "Update access_time in " << updatedRowCount << " of " << initialRowCount << " metadata rows with " << conflictCount << " transaction conflicts";
        }

        static inline void AddDataKeys(NYT::TNode::TListType& keys, const TString& hash, size_t chunks_count) {
            for (size_t i = 0; i < chunks_count; ++i) {
                keys.push_back(NYT::TNode()("hash", hash)("chunk_i", i));
            }
        }

        void DeleteRows(TConfigureResultPtr config, const NYT::TNode::TListType& deleteMetadataKeys, const NYT::TNode::TListType& deleteDataKeys) {
            Y_ENSURE(!ReadOnly_);
            auto opts = MakeTransactionOpts<TDeleteRowsOptions>(config);
            if (!deleteMetadataKeys.empty()) {
                RetryYtError([&] {
                    config->MainCluster.Client->DeleteRows(NYT::JoinYPaths(config->MainCluster.DataDir, METADATA_TABLE), deleteMetadataKeys, opts);

                });
            }
            if (!deleteDataKeys.empty()) {
                RetryYtError([&] {
                    config->MainCluster.Client->DeleteRows(NYT::JoinYPaths(config->MainCluster.DataDir, DATA_TABLE), deleteDataKeys, opts);
                });
            }
        }

        void PutStat(TConfigureResultPtr config, const TString& key, const NYT::TNode& value) {
            DEBUG_LOG << "Put to stat table key=" << key << ":" << (ReadOnly_ ? " (dry run)" : "") << "\n" << NYT::NodeToYsonString(value, ::NYson::EYsonFormat::Pretty);
            if (ReadOnly_) {
                return;
            }
            NYT::TYPath statTable = NYT::JoinYPaths(config->MainCluster.DataDir, STAT_TABLE);
            bool dynamic = config->MainCluster.Client->Get(NYT::JoinYPaths(statTable, "@dynamic")).AsBool();
            auto row = NYT::TNode()("timestamp", ToYtTimestamp(TInstant::Now()))("key", key)("value", value);
            if (dynamic) {
                row["salt"] = RandomNumber<ui64>();
                RetryYtError([&] {
                    config->MainCluster.Client->InsertRows(statTable, {row}, MakeTransactionOpts<TInsertRowsOptions>(config));
                });
            } else {
                NYT::TRichYPath richPath = NYT::TRichYPath(statTable).Append(true);
                RetryYtError([&] {
                    auto writer = config->MainCluster.Client->CreateTableWriter<NYT::TNode>(richPath);
                    writer->AddRow(row);
                    writer->Finish();
                });
            }
        }

        static inline void CreateDynamicTable(
            NYT::IClientPtr ytc,
            const TString& tableName,
            NYT::ENodeType tableType,
            const TStringBuf tableSchema,
            ui64 tabletCount,
            bool ignoreExisting,
            bool tracked,
            const NYT::TNode& addAttrs = {}
        ) {
            auto schema = NodeFromYsonString(tableSchema);

            NYT::TNode pivotKeys = NYT::TNode::CreateList();
            pivotKeys.Add(NYT::TNode::CreateList()); // the first pivot is always an empty list
            if (tabletCount > 1) {
                // It's not too hard to support a signed column but this is useless
                Y_ENSURE(schema.At(0).ChildAsString("type") == "uint64", "First key column must have uint64 type");
                // Divide (1 << 64) by tabletCount
                ui64 step = Max<ui64>() / tabletCount;
                ui64 rem = Max<ui64>() % tabletCount + 1;
                if (rem == tabletCount) {
                    rem = 0;
                    ++step;
                }
                for (ui64 i = 1; i < tabletCount; ++i) {
                    ui64 pivot = step * i + rem * i / tabletCount;
                    pivotKeys.Add(NYT::TNode::CreateList().Add(pivot));
                }
            }

            auto tableAttrs = NYT::TNode()
                ("dynamic", true)
                ("schema", schema)
                ("optimize_for", "scan")  // recommended by the YT team
                ("compression_codec", "none")  // all big data is written in compressed form on the client side
                ("pivot_keys", pivotKeys);

            if (tracked) {
                Y_ENSURE(tableType == NYT::ENodeType::NT_REPLICATED_TABLE, "The replicated table tracker only applies to a replicated table");
                tableAttrs("replicated_table_options", NYT::TNode()("enable_replicated_table_tracker", true));
            }

            if (!addAttrs.IsUndefined()) {
                for (const auto& [attrName, attrVal] : addAttrs.AsMap()) {
                    tableAttrs(attrName, attrVal);
                }
            }

            ytc->Create(tableName, tableType, NYT::TCreateOptions().Attributes(tableAttrs).IgnoreExisting(ignoreExisting));
        }

        static inline void MountTables(NYT::IClientPtr ytc, std::span<const NYT::TYPath> tablePaths) {
            for (const NYT::TYPath& path : tablePaths) {
                ytc->MountTable(path);
            }
            for (const NYT::TYPath& path : tablePaths) {
                NYT::WaitForTabletsState(ytc, path, NYT::ETabletState::TS_MOUNTED);
            }
        }

        static inline void UnmountTables(NYT::IClientPtr ytc, std::span<const NYT::TYPath> tablePaths) {
            for (const NYT::TYPath& path : tablePaths) {
                ytc->UnmountTable(path);
            }
            for (const NYT::TYPath& path : tablePaths) {
                NYT::WaitForTabletsState(ytc, path, NYT::ETabletState::TS_UNMOUNTED);
            }
        }

        void SleepAfterTransactionConflict() {
            Sleep(TDuration::Seconds(RandomNumber<double>() * 0.1 + 0.05));
        }

        static inline bool IsYtError(const std::exception& e) {
            return dynamic_cast<const NYT::TErrorResponse*>(&e) || dynamic_cast<const NYT::TTransportError*>(&e);
        }

        static inline bool IsYtAuthError(const std::exception& e) {
            static constexpr int authErrorCodes[] = {
                NYT::NClusterErrorCodes::NRpc::InvalidCredentials,
                NYT::NClusterErrorCodes::NSecurityClient::AuthenticationError,
                NYT::NClusterErrorCodes::NSecurityClient::AuthorizationError,
            };

            if (auto error = dynamic_cast<const NYT::TErrorResponse*>(&e)) {
                for (auto code : authErrorCodes) {
                    if (error->GetError().ContainsErrorCode(code)) {
                        return true;
                    }
                }
            }
            return false;
        }

        static inline bool IsPermanentError(const std::exception& e) {
            if (IsYtAuthError(e)) {
                return true;
            }
            if (auto error = dynamic_cast<const NYT::TErrorResponse*>(&e)) {
                // "Error resolving path ..."
                if (error->GetError().ContainsErrorCode(NYT::NClusterErrorCodes::NYTree::ResolveError)) {
                    return true;
                }
            }
            return false;
        }

        bool ProbeMeta(TConfigureResultPtr config, TPrepareResultPtr prepareResultPtr, const TString& selfUid, const TString& uid) {
            auto startTime = TInstant::Now();
            Y_DEFER {
                Metrics_.IncTime("probe-meta-before-put", startTime, TInstant::Now());
            };

            NYT::TNode keyRow{};
            if (config->Version >= 3) {
                keyRow("self_uid", selfUid)("uid", uid);
            } else {
                keyRow("uid", uid);
            }
            auto rows = RetryYtError([&] {
                return prepareResultPtr->Task->Client->LookupRows(
                    NYT::JoinYPaths(prepareResultPtr->Task->DataDir, METADATA_TABLE),
                    NYT::TNode::TListType{keyRow},
                    TLookupRowsOptions().Columns({"uid", "data_size"})
                );
            });
            if (rows.empty()) {
                return false;
            }
            Metrics_.IncCounter("skip-put");
            if (auto data_size = SafeChildAs<ui64>(rows.back(), "data_size")) {
                Metrics_.IncDataSize("skip-put", data_size);
            }
            return true;
        }

        void PrepareData(const TFsPath& archivePath, const TFsPath& rootDir, const TVector<TFsPath>& files, const TString& codec) {
            TFileOutput out{archivePath};
            IOutputStream* dest = &out;

            std::unique_ptr<NUCompress::TCodedOutput> encoder;
            if (codec) {
                encoder = std::make_unique<NUCompress::TCodedOutput>(&out, NBlockCodecs::Codec(codec));
                dest = encoder.get();
            }
            NTar::Tar(*dest, rootDir, files);
        }

        void WriteMeta(
            TConfigureResultPtr config,
            const TString& selfUid,
            const TString& uid,
            const TString& cuid,
            const TString& name,
            const TString& codec,
            ui64 dataSize,
            const TString& hash,
            ui64 chunksCount
        ) {
            auto nowYtTs = ToYtTimestamp(TInstant::Now());
            auto metaRow = NYT::TNode()
                ("uid", uid)
                ("name", name)
                ("hash", hash)
                ("chunks_count", chunksCount)
                ("codec", codec)
                ("data_size", dataSize)
                ("hostname", HostName())
                ("GSID", GSID_)
                ("access_time", nowYtTs);

            if (config->Version >= 3) {
                metaRow
                    ("self_uid", selfUid)
                    ("cuid", cuid)
                    ("create_time", nowYtTs);
            }

            SafeInsertRows(config, METADATA_TABLE, NYT::TNode::TListType{metaRow}, config->MetadataKeyColumns);
        }

        // replace or on transaction conflict do nothing
        void SafeInsertRows(
            TConfigureResultPtr config,
            const NYT::TYPath& tableName,
            NYT::TNode::TListType&& rows,
            const TVector<TString>& keyColumns
        ) {
            NYT::TYPath tablePath = NYT::JoinYPaths(config->MainCluster.DataDir, tableName);

            const auto insertOpts = MakeTransactionOpts<NYT::TInsertRowsOptions>(config);
            const auto lookupOpts = NYT::TLookupRowsOptions().Columns(keyColumns).KeepMissingRows(true);
            while (!rows.empty()) {
                bool transactionConflict = RetryYtError([&] {
                    try {
                        config->MainCluster.Client->InsertRows(tablePath, rows, insertOpts);
                        return false;
                    } catch (const NYT::TErrorResponse& e) {
                        if (e.GetError().ContainsErrorCode(NYT::NClusterErrorCodes::NTabletClient::TransactionLockConflict)) {
                            return true;
                        }
                        throw;
                    }
                });

                if (!transactionConflict || rows.size() == 1) {
                    return;
                }

                SleepAfterTransactionConflict();

                NYT::TNode::TListType keys{};
                for (auto& row : rows) {
                    NYT::TNode keyRow{};
                    for (const auto& column: keyColumns) {
                        keyRow(column, row[column]);
                    }
                    keys.push_back(std::move(keyRow));
                }
                NYT::TNode::TListType existingRows = RetryYtError([&] {
                    return config->MainCluster.Client->LookupRows(tablePath,keys, lookupOpts);
                });

                Y_ASSERT(existingRows.size() == rows.size());

                NYT::TNode::TListType&& oldRows{std::move(rows)};
                rows.clear();
                for (size_t i = 0; i < oldRows.size(); ++i) {
                    if (existingRows[i].IsNull()) {
                        rows.push_back(std::move(oldRows[i]));
                    }
                }
            }
        }

    private:
        TYtConnectOptions ConnectOptions_;
        void* Owner_;
        std::atomic_bool ReadOnly_;
        bool CheckSize_;
        TMaxCacheSize MaxCacheSize_;
        TDuration Ttl_;
        TVector<TNameReTtl> NameReTtls_;
        TString OperationPool_;
        TDuration RetryTimeLimit_;
        TDuration PrepareTimeout_;
        // TODO (YA-2800) remove maybe_unused later
        [[maybe_unused]] bool ProbeBeforePut_;
        [[maybe_unused]] size_t ProbeBeforePutMinSize_;
        ECritLevel CritLevel_;
        TString GSID_;

        std::atomic_bool Disabled_{};
        TAdaptiveThreadPool ThreadPool_{};
        TInitializeControl InitializeControl_{};
        TPrepareControl PrepareControl_{};
        TRetryPolicyPtr RetryPolicy_;
        TMetricsManager Metrics_;
        TMemorySemaphore MemSem_{DEFAULT_MAX_MEMORY_USAGE};
    };

    TYtStore::TYtStore(const TString& proxy, const TString& dataDir, const TYtStoreOptions& options) {
        Impl_ = std::make_unique<TImpl>(proxy, dataDir, options);
    }

    TYtStore::~TYtStore() {
        Shutdown();
    };

    bool TYtStore::Disabled() const noexcept {
        return Impl_->Disabled();
    }

    bool TYtStore::ReadOnly() const noexcept {
        return Impl_->ReadOnly();
    }

    void TYtStore::Prepare(TPrepareOptionsPtr options) {
        Impl_->Prepare(options);
    }

    bool TYtStore::Has(const TString& uid) {
        return Impl_->Has(uid);
    }

    bool TYtStore::TryRestore(const TString& uid, const TString& intoDir) {
        return Impl_->TryRestore(uid, intoDir);
    }

    bool TYtStore::Put(const TPutOptions& options) {
        return Impl_->Put(options);
    }

    TYtStore::TMetrics TYtStore::GetMetrics() const {
        return Impl_->GetMetrics();
    }

    void TYtStore::Strip() {
        Impl_->Strip();
    }

    void TYtStore::DataGc(const TDataGcOptions& options) {
        Impl_->DataGc(options);
    }

    void TYtStore::Shutdown() noexcept {
        Impl_->Shutdown();
    }

    void TYtStore::ValidateRegexp(const TString& re) {
        TImpl::ValidateRegexp(re);
    }

    void TYtStore::CreateTables(const TString& proxy, const TString& dataDir, const TCreateTablesOptions& options) {
        TImpl::CreateTables(proxy, dataDir, options);
    }

    void TYtStore::ModifyTablesState(const TString& proxy, const TString& dataDir, const TModifyTablesStateOptions& options) {
        TImpl::ModifyTablesState(proxy, dataDir, options);
    }

    void TYtStore::PutStat(const TString& key, const TString& value) {
        Impl_->PutStat(key, value);
    }

    void TYtStore::ModifyReplica(
        const TString& proxy,
        const TString& dataDir,
        const TString& replicaProxy,
        const TString& replicaDataDir,
        const TModifyReplicaOptions& options
    ) {
        TImpl::ModifyReplica(proxy, dataDir, replicaProxy, replicaDataDir, options);
    }

    std::unique_ptr<TYtStore::TInternalState> TYtStore::GetInternalState() {
        return Impl_->GetInternalState();
    }
}
