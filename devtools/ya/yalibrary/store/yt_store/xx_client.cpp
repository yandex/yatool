#include "xx_client.hpp"

#include <contrib/libs/libarchive/libarchive/archive.h>
#include <contrib/libs/libarchive/libarchive/archive_entry.h>

#include <library/cpp/blockcodecs/core/codecs.h>
#include <library/cpp/logger/global/global.h>
#include <library/cpp/regex/pcre/regexp.h>
#include <library/cpp/retry/retry.h>
#include <library/cpp/threading/cancellation/cancellation_token.h>
#include <library/cpp/ucompress/reader.h>
#include <library/cpp/ucompress/writer.h>
#include <library/cpp/yson/node/node_io.h>
#include <yt/cpp/mapreduce/client/client.h>
#include <yt/cpp/mapreduce/common/retry_lib.h>
#include <yt/cpp/mapreduce/http_client/raw_client.h>
#include <yt/cpp/mapreduce/interface/logging/logger.h>
#include <yt/cpp/mapreduce/util/ypath_join.h>

#include <util/digest/city_streaming.h>
#include <util/folder/path.h>
#include <util/generic/guid.h>
#include <util/generic/scope.h>
#include <util/generic/size_literals.h>
#include <util/string/join.h>
#include <util/stream/format.h>
#include <util/stream/printf.h>
#include <util/system/env.h>
#include <util/system/thread.h>
#include <util/thread/pool.h>

#include <utility>

using namespace NYT;

// see .pyx
extern void YaYtStoreLoggingHook(ELogPriority priority, const char* msg, size_t size);
extern void YaYtStoreDisableHook(void* callback, const TString& errorType, const TString& errorMessage);

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
                YaYtStoreLoggingHook(ELogPriority::TLOG_DEBUG, msg.data(), msg.length());
            }
        }
    };

    struct TLogBridgeBackend : public TLogBackend {
    public:
        TLogBridgeBackend() = default;
        ~TLogBridgeBackend() override = default;

        void WriteData(const TLogRecord& record) override {
            YaYtStoreLoggingHook(record.Priority, record.Data, record.Len);
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

    class TRetryConfigProvider: public IRetryConfigProvider {
    public:
        TRetryConfigProvider(TDuration retryTimeLimit) {
            RetryConfig_ = TRetryConfig{.RetriesTimeLimit = retryTimeLimit};
        }

        TRetryConfig CreateRetryConfig() {
            return RetryConfig_;
        }

    private:
        TRetryConfig RetryConfig_;
    };
}

YtStore::YtStore(const char* yt_proxy, const char* yt_dir, const char* yt_token, TDuration retry_time_limit) {
    constexpr auto RETRY_INTERVAL = TDuration::MilliSeconds(500);
    InitializeLogger();
    auto config = NYT::TConfig::Get();
    config->ConnectTimeout = TDuration::Seconds(5);
    config->SocketTimeout = TDuration::Seconds(5);
    config->RetryInterval = RETRY_INTERVAL;
    config->RetryCount = 5;
    NYT::TCreateClientOptions clientOpts = NYT::TCreateClientOptions().Token(yt_token);
    if (retry_time_limit) {
        config->RetryCount = retry_time_limit / RETRY_INTERVAL + 1;
        // Limit total retry time because each attempt consumes non-zero time
        clientOpts.RetryConfigProvider(MakeIntrusive<TRetryConfigProvider>(retry_time_limit));
    }
    this->Client = NYT::CreateClient(yt_proxy, clientOpts);
    this->YtDir = yt_dir;
}

YtStore::~YtStore() {
}

struct ChunkFetcher: public IWalkInput {
    YtStore& Parent;
    const YtStoreClientRequest& Req;
    YtStoreClientResponse& Rsp;
    int ChunkNo;
    std::unordered_map<int, NYT::TNode> Chunks;
    std::optional<TStreamingCityHash64> Sch;

    void Fetch(uint64_t chunk_i, uint64_t chunk_j = ~0) {
        TVector<NYT::TNode> keys;
        keys.push_back(NYT::TNode()("hash", Req.Hash)("chunk_i", chunk_i));
        if (~chunk_j && chunk_j != chunk_i) {
            keys.push_back(NYT::TNode()("hash", Req.Hash)("chunk_i", chunk_j));
        }
        TNode::TListType rows;
        try {
            rows = Parent.Client->LookupRows(Parent.YtDir + "/data", keys, TLookupRowsOptions().Timeout(TDuration::Seconds(60)));
        } catch (std::exception& e) {
            Rsp.NetworkErrors = true;
            throw;
        }
        for (auto& row : rows) {
            Chunks[row.ChildAsUint64("chunk_i")] = std::move(row);
        }
        for (const auto& key : keys) {
            auto chunk = key.ChildAsUint64("chunk_i");
            if (!Chunks.contains(chunk)) {
                ythrow yexception() << "Failed to fetch chunk #" << chunk << " for hash " << Req.Hash;
            }
        }
    }

    ChunkFetcher(YtStore& parent, const YtStoreClientRequest& req, YtStoreClientResponse& rsp)
        : Parent(parent)
        , Req(req)
        , Rsp(rsp)
        , ChunkNo(0)
    {
        Fetch(0, req.Chunks - 1);
        if (req.Chunks == 1) {
            auto real_hash = CityHash64(Chunks[0].ChildAsString("data"));
            if (stoull(std::string(req.Hash)) != real_hash) {
                ythrow yexception() << "Hash mismatch: " << req.Hash << " != " << real_hash;
            }
        } else {
            const auto& last = Chunks[req.Chunks - 1].ChildAsString("data");
            if (last.size() >= 64) {
                Sch.emplace(
                    req.DataSize,
                    Chunks[0].ChildAsString("data").c_str(),
                    last.c_str() + last.size() - 64);
            } else {
                if (req.Chunks > 2) {
                    Fetch(req.Chunks - 2);
                }
                const auto& pre_last = Chunks[req.Chunks - 2].ChildAsString("data");
                char tail[64];
                memcpy(tail, pre_last.c_str() + pre_last.size() - 64 + last.size(), 64 - last.size());
                memcpy(tail + 64 - last.size(), last.c_str(), last.size());
                Sch.emplace(
                    req.DataSize,
                    Chunks[0].ChildAsString("data").c_str(),
                    tail);
            }
        }
    }
    size_t DoUnboundedNext(const void** ptr) override {
        if (ChunkNo == Req.Chunks) {
            *ptr = nullptr;
            return 0;
        } else {
            Chunks.erase(ChunkNo - 1);
            const auto& cur = Chunks[ChunkNo].ChildAsString("data");
            if (Sch.has_value()) {
                Sch->Process(cur.c_str(), cur.size());
            }
            if (++ChunkNo == Req.Chunks) {
                if (Sch.has_value()) {
                    auto real_hash = (*Sch)();
                    if (stoull(std::string(Req.Hash)) != real_hash) {
                        ythrow yexception() << "Hash mismatch: " << Req.Hash << " != " << real_hash;
                    }
                }
            } else if (!Chunks.contains(ChunkNo)) {
                Fetch(ChunkNo);
            }
            *ptr = cur.c_str();
            return cur.size();
        }
    }
};

struct YtStoreGetter {
    YtStore& Parent;
    const YtStoreClientRequest& Req;
    ChunkFetcher Fetcher;
    std::optional<NUCompress::TDecodedInput> Decoder;
    TTempBuf Buf;
    size_t DecodedSize;

    YtStoreGetter(YtStore& parent, const YtStoreClientRequest& req, YtStoreClientResponse& rsp)
        : Parent(parent)
        , Req(req)
        , Fetcher(parent, req, rsp)
        , DecodedSize(0)
    {
        if (req.Codec != nullptr && strcmp(req.Codec, "")) {
            Decoder.emplace(&Fetcher);
            Buf = TTempBuf(64 << 10);
        }
    }

    la_ssize_t Read(const void** buffer) {
        if (Y_LIKELY(Decoder.has_value())) {
            *buffer = Buf.Data();
            auto len_decoded = Decoder->Read(Buf.Data(), Buf.Size());
            DecodedSize += len_decoded;
            return len_decoded;
        }
        return Fetcher.DoUnboundedNext(buffer);
    }

    static la_ssize_t Callback(struct archive* a, void* client_data, const void** buffer) {
        Y_UNUSED(a);
        return reinterpret_cast<YtStoreGetter*>(client_data)->Read(buffer);
    }
};

struct ArchiveWriter {
    TFileOutput Output;
    std::optional<NUCompress::TCodedOutput> Encoder;
    size_t RawWritten;

    ArchiveWriter(const char *path, const char *codec): Output(path), RawWritten(0) {
        if (codec != nullptr && strcmp(codec, "")) {
            Encoder.emplace(&Output, NBlockCodecs::Codec(codec));
        }
    }

    la_ssize_t Write(const void *buffer, size_t length) {
        RawWritten += length;
        if (Encoder.has_value()) {
            Encoder->Write(buffer, length);
        } else {
            Output.Write(buffer, length);
        }
        return length;
    }

    static la_ssize_t Callback(struct archive *a, void *client_data, const void *buffer, size_t length) {
        Y_UNUSED(a);
        return reinterpret_cast<ArchiveWriter*>(client_data)->Write(buffer, length);
    }
};

namespace {

static inline void Check(int code, struct archive *a, const char *msg) {
    if (code != ARCHIVE_OK) {
        ythrow yexception() << msg << ": " << archive_error_string(a);
    }
}

const char *MSG_CREATE = "Failed to create tar";
const char *MSG_OPEN = "Failed to open tar";
const char *MSG_EXTRACT = "Failed to extract tar";

}

void YtStore::DoTryRestore(const YtStoreClientRequest& req, YtStoreClientResponse& rsp) {
    struct archive* a = archive_read_new();
    struct archive* writer = archive_write_disk_new();
    struct archive_entry* entry = nullptr;
    const void* buf;
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
    Y_DEFER {
        archive_read_free(a);
        archive_write_free(writer);
    };
    rsp.NetworkErrors = false;
    try {
        YtStoreGetter getter(*this, req, rsp);
        TFsPath root_dir = req.IntoDir;
        Check(archive_read_open(a, &getter, nullptr, YtStoreGetter::Callback, nullptr), a, MSG_OPEN);
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            size_t size;
            la_int64_t offset;
            int r;
            archive_entry_set_pathname(entry, (root_dir / archive_entry_pathname(entry)).c_str());
            if (const char* src = archive_entry_hardlink(entry); src) {
                archive_entry_set_hardlink(entry, (root_dir / src).c_str());
            }
            Check(archive_write_header(writer, entry), writer, MSG_EXTRACT);
            while ((r = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK) {
                Check(archive_write_data_block(writer, buf, size, offset), writer, MSG_EXTRACT);
            }
            if (r != ARCHIVE_EOF) {
                ythrow yexception() << "Failed to read archive: " << archive_error_string(a);
            }
            Check(archive_write_finish_entry(writer), writer, MSG_EXTRACT);
        }
        // Ensure we readed all from cache (and checked hash thus)
        // Possibly will never happen IRL but useful for testing
        while (Y_UNLIKELY(getter.Fetcher.DoUnboundedNext(&buf) != 0)) {
        };
        rsp.Success = true;
        rsp.DecodedSize = getter.DecodedSize;
    } catch (yexception exc) {
        rsp.Success = false;
        snprintf(rsp.ErrorMsg, sizeof(rsp.ErrorMsg), "%s", exc.what());
    }
}

void YtStore::PrepareData(const YtStorePrepareDataRequest& req, YtStorePrepareDataResponse& rsp) {
    try {
        struct archive* a = archive_write_new();
        archive_write_add_filter_none(a);
        archive_write_set_format_gnutar(a);
        archive_write_set_bytes_in_last_block(a, 1);
        ArchiveWriter writer(req.OutPath, req.Codec);
        Y_DEFER {
            archive_write_free(a);
        };
        Check(archive_write_open2(a, &writer, nullptr, ArchiveWriter::Callback, nullptr, nullptr), a, MSG_CREATE);
        TFsPath root_dir = req.RootDir;
        struct archive_entry* entry = nullptr;
        for(const auto &path: req.Files) {
            struct archive *disk = archive_read_disk_new();
            Y_DEFER {
                archive_read_free(disk);
            };
            Check(archive_read_disk_open(disk, path), disk, "Failed to read file");

            entry = archive_entry_new();
            Y_DEFER {
                archive_entry_free(entry);
            };
            Check(archive_read_next_header2(disk, entry), disk, "Failed to read file");

            archive_entry_set_pathname(entry, TFsPath(archive_entry_pathname(entry)).RelativePath(root_dir).c_str());
            archive_entry_set_mtime(entry, 0, 0);
            archive_entry_set_uid(entry, 0);
            archive_entry_set_gid(entry, 0);

            Check(archive_write_header(a, entry), a, "Failed to write tar");
            if (!archive_entry_hardlink(entry) && !archive_entry_symlink(entry)) {
                TMappedFileInput file_input(path);
                if (file_input.Avail()) {
                    auto write = archive_write_data(a, file_input.Buf(), file_input.Avail());
                    if (write == ARCHIVE_FATAL || write <=0) {
                        ythrow yexception() << "Failed to write tar: " << path << " " << write << archive_error_string(a);
                    }
                }
            }
        }
        rsp.RawSize = writer.RawWritten;
        rsp.Success = true;
    } catch (yexception exc) {
        rsp.Success = false;
        snprintf(rsp.ErrorMsg, sizeof(rsp.ErrorMsg), "%s", exc.what());
    }
}

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

    namespace {
        using TYtTimestamp = ui64;
        using TKnownHashesSet = THashSet<TString>;

        TYtTimestamp ToYtTimestamp(TInstant time) {
            return time.MicroSeconds();
        }

        TInstant FromYtTimestamp(TYtTimestamp ts) {
            return TInstant::MicroSeconds(ts);
        }

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
            TStripReducer(const TVector<TString> metaKeyColumns, bool writeRemainingRows)
                : MetaKeyColumns_{metaKeyColumns}
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
                        for (const TString& column : MetaKeyColumns_) {
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

            Y_SAVELOAD_JOB(MetaKeyColumns_, WriteRemainingRows_);
        private:
            static constexpr size_t STAT_TABLE_INDEX = 1;
            static constexpr size_t REMAINING_ROWS_TABLE_INDEX = 2;

            TVector<TString> MetaKeyColumns_;
            bool WriteRemainingRows_;
            TStripRawStat StripStat_{};
        };
        REGISTER_REDUCER(TStripReducer)

        TString PrettyPrint(TDuration val) {
            TVector<TString> result{};
            auto seconds = val.Seconds();
            if (seconds >= 86400) {
                result.push_back(ToString(seconds / 86400) + "d");
                seconds %= 86400;
            }
            if (seconds > 0) {
                result.push_back(ToString(seconds / 3600) + "h");
                seconds %= 3600;
            }
            if (seconds > 0) {
                result.push_back(ToString(seconds / 60) + "m");
                seconds %= 60;
            }
            if (seconds > 0 || result.empty()) {
                result.push_back(ToString(seconds) + "s");
            }
            return JoinSeq(" ", result);
        }

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

    class TYtStore2::TImpl {
    public:
        TImpl(const TString proxy, const TString& dataDir, const TYtStore2Options& options)
            : Token_{options.Token}
            , ReadOnly_(options.ReadOnly)
            , OnDisable_{options.OnDisable}
            , MaxCacheSize_{options.MaxCacheSize}
            , Ttl_(options.Ttl)
            , NameReTtls_{options.NameReTtls}
            , OperationPool_{options.OperationPool}
            , Durability_{options.SyncDurability ? NYT::EDurability::Sync : NYT::EDurability::Async}
        {
            InitializeLogger();
            MainCluster_ = {.Proxy = proxy, .DataDir = dataDir, .Client = ConnectToCluster(proxy)};
            ThreadPool_.Start();
            ThreadPool_.SetMaxIdleTime(TDuration::Seconds(2));

            // Validate
            for (const TNameReTtl& item : NameReTtls_) {
                ValidateRegexp(item.NameRe);
            }

            if (options.RetryTimeLimit) {
                RetryPolicy_ = TRetryPolicy::GetExponentialBackoffPolicy(
                    [](const yexception&) {return ERetryErrorClass::ShortRetry;},
                    RETRY_MIN_DELAY,
                    RETRY_MIN_DELAY, // not used
                    RETRY_MAX_DELAY,
                    std::numeric_limits<size_t>::max(),
                    options.RetryTimeLimit,
                    RETRY_SCALE_FACTOR
                );
            } else {
                RetryPolicy_ = TRetryPolicy::GetExponentialBackoffPolicy(
                    [](const yexception&) {return ERetryErrorClass::ShortRetry;},
                    RETRY_MIN_DELAY,
                    RETRY_MIN_DELAY, // not used
                    RETRY_MAX_DELAY,
                    RETRY_MAX_COUNT,
                    TDuration::Max(),
                    RETRY_SCALE_FACTOR
                );
            }
            auto initializePromise = NThreading::NewPromise<void>();
            InitializeFuture_ = initializePromise.GetFuture();
            ThreadPool_.SafeAddFunc([this, initializePromise] {
                TThread::SetCurrentThreadName("YtStore::Initialize");
                Initialize(initializePromise);
            });
        }

        ~TImpl() = default;

        void WaitInitialized() {
            InitializeFuture_.GetValueSync();
        }

        // NOTE: Called with GIL.
        bool Disabled() const noexcept {
            return Disabled_.load(std::memory_order::relaxed);
        }

        void Strip() {
            WaitInitialized();
            if (!Ttl_ && NameReTtls_.empty() && std::visit([](const auto& v) {return v == 0;}, MaxCacheSize_)) {
                INFO_LOG << "Neither ttl nor max_cache_size was specified. Nothing to do";
                return;
            }
            if (ReadOnly_) {
                INFO_LOG << "Strip runs in readonly mode (dry run)";
            }

            auto [opClient, opDataDir] = GetOpCluster();
            Y_ENSURE(opClient && opDataDir);

            size_t maxCacheSize = GetMaxCacheSize(opClient, opDataDir);

            INFO_LOG << "Desired max age: " << (Ttl_ ? PrettyPrint(Ttl_) : "-");
            for (const auto& item : NameReTtls_) {
                INFO_LOG << "Desired max age: " << PrettyPrint(item.Ttl) << " for " << item.NameRe;
            }
            INFO_LOG << "Desired data size: " << (maxCacheSize ? ToString(HumanReadableSize(maxCacheSize, ESizeFormat::SF_BYTES)).c_str() : "-");

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

            TVector<TString> metaKeyColumns{};
            for (const NYT::TNode& column : MetadataSchema_.AsList()) {
                if (column.HasKey("sort_order") && !column.HasKey("expression")) {
                    metaKeyColumns.push_back(column.ChildAsString("name"));
                }
            }
            TVector<TString> mappedColumns{metaKeyColumns};
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
            auto reducer = MakeIntrusive<TStripReducer>(metaKeyColumns, stripByDataSize);
            DEBUG_LOG << "Start MapReduce";
            tx->MapReduce(mrSpec, mapper, reducer);

            // Calc Stat
            TStripRawStat rawStat{};
            for (auto reader = tx->CreateTableReader<NYT::TNode>(infoTable); reader->IsValid(); reader->Next()) {
                rawStat.UpdateFromNode(reader->GetRow());
            }
            TDuration currentAge = now - Min(now, FromYtTimestamp(rawStat.InitialMinAccessTime));

            INFO_LOG << "Current net data size: " << HumanReadableSize(rawStat.InitialDataSize * REPLICATION_FACTOR, ESizeFormat::SF_BYTES);
            INFO_LOG << "Current max age: " << PrettyPrint(currentAge);

            DEBUG_LOG << "Start deleting rows by access time" << (ReadOnly_ ? " (dry run)" : "");

            size_t removedMetadataRowCount{};
            for (auto reader = tx->CreateTableReader<NYT::TNode>(removeTable); reader->IsValid(); ) {
                NYT::TNode::TListType deleteMetaKeys{};
                NYT::TNode::TListType deleteDataKeys{};
                while (reader->IsValid() && deleteMetaKeys.size() < REMOVE_BATCH_SIZE) {
                    const NYT::TNode& row = reader->GetRow();
                    if (row.HasKey("hash")) {
                        AddDataKeys(deleteDataKeys, row.ChildAsString("hash"), row.ChildAsUint64("chunks_count"));
                    } else {
                        NYT::TNode keys{};
                        for (const TString& column : metaKeyColumns) {
                            keys[column] = row.At(column);
                        }
                        deleteMetaKeys.push_back(std::move(keys));
                        ++removedMetadataRowCount;
                    }
                    reader->Next();
                }

                if (!ReadOnly_) {
                    DeleteRows(deleteMetaKeys, deleteDataKeys);
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
                    NYT::TNode::TListType deleteMetaKeys{};
                    NYT::TNode::TListType deleteDataKeys{};
                    bool done = false;
                    while (reader->IsValid() && deleteMetaKeys.size() < REMOVE_BATCH_SIZE) {
                        const NYT::TNode& row = reader->GetRow();
                        NYT::TNode keys{};
                        for (const TString& column : metaKeyColumns) {
                            keys[column] = row.At(column);
                        }
                        deleteMetaKeys.push_back(std::move(keys));
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
                        DeleteRows(deleteMetaKeys, deleteDataKeys);
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
            INFO_LOG << "Max age after clean: " << PrettyPrint(remainingAge);
            INFO_LOG << "Removed row count from metadata: " << removedMetadataRowCount;
            INFO_LOG << "Removed hash count from data: " << (rawStat.InitialFileCount - rawStat.RemainingFileCount);
            INFO_LOG << "Removed net data size: " << HumanReadableSize((rawStat.InitialDataSize - rawStat.RemainingDataSize) * REPLICATION_FACTOR, ESizeFormat::SF_BYTES);

            // Put cleaner stat
            auto makeStatNode = [](TDuration ttl, size_t dataSize, size_t fileCount) {
                return NYT::TNode()
                ("age", i64(ttl.Seconds()))
                ("data_size", i64(dataSize))
                ("file_count", i64(fileCount));
            };
            NYT::TNode statNode = NYT::TNode()
                ("before_clean", makeStatNode(currentAge, rawStat.InitialDataSize, rawStat.InitialFileCount))
                ("after_clean", makeStatNode(remainingAge, rawStat.RemainingDataSize, rawStat.RemainingFileCount));
            DEBUG_LOG << "Put to stat table:" << (ReadOnly_ ? " (dry run)" : "") << "\n" << NYT::NodeToYsonString(statNode, ::NYson::EYsonFormat::Pretty);
            if (!ReadOnly_) {
                PutStat("cleaner", statNode);
            }
            // Yes, it's the default behaviour and is optional but added to demonstrate intent
            tx->Abort();
        }

        void DataGc() {
            WaitInitialized();
            if (ReadOnly_) {
                INFO_LOG << "Data GC runs in readonly mode (dry run)";
            }

            auto [opClient, opDataDir] = GetOpCluster();
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
            if (pivotKeys.size() > 1) {
                ui64 lastPivotBegin = pivotKeys.back()[0].AsUint64();
                ui64 maxKeyValue = lastPivotBegin + lastPivotBegin / (pivotKeys.size() - 1);
                auto dataSize = dataAttrs.ChildAsInt64("uncompressed_data_size");
                auto rangeCount = Max<i64>(dataSize / DATA_GC_DATA_SIZE_PER_KEY_RANGE, 1);
                ui64 step = maxKeyValue / rangeCount;
                ui64 rem = maxKeyValue % rangeCount;
                for (int i = 1; i < rangeCount; i++) {
                    ui64 end = step * i + rem * i / rangeCount;
                    keyRanges.push_back({begin, end});
                    begin = end;
                }
            }
            keyRanges.push_back({begin, Max<ui64>()});
            INFO_LOG << "Data table scanning split into " << keyRanges.size() << " operations";

            const TYtTimestamp timeThreshold = ToYtTimestamp(TInstant::Now() - DATA_GC_MIN_AGE);
            DEBUG_LOG << "Time threshold=" <<  timeThreshold;

            size_t deletedRowCount = 0;
            // It's ok to reuse the same table for all operations
            NYT::TYPath orphanRowsTable = NYT::JoinYPaths(TMP_DIR, CreateGuidAsString());
            for (const auto& keyRange : keyRanges) {
                INFO_LOG << "Range " << keyRange << ": start";
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
                    .EnableKeyGuarantee(false)
                    // YT overestimate required job count because of large 'data' column size
                    .DataSizePerJob(DATA_GC_DATA_SIZE_PER_JOB);
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
                        DeleteRows({}, keys);
                    }
                }
                INFO_LOG << "Range " << keyRange << ": " << foundRowCount << " orphan rows found";
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
            // Yes, it's the default behaviour and is optional but added to demonstrate intent
            tx->Abort();
        }

        static inline void ValidateRegexp(const TString& re) {
            TRegExMatch x{re};
        }

    private:
        static inline const NYT::TYPath TMP_DIR = "//tmp";
        static inline const NYT::TYPath METADATA_TABLE = "metadata";
        static inline const NYT::TYPath DATA_TABLE = "data";
        static inline const NYT::TYPath STAT_TABLE = "stat";
        static inline const size_t REMOVE_BATCH_SIZE = 10'000;
        static constexpr size_t REPLICATION_FACTOR = 3;
        static constexpr TDuration RETRY_MIN_DELAY = TDuration::Seconds(0.1);
        static constexpr double RETRY_SCALE_FACTOR = 1.3;
        static constexpr TDuration RETRY_MAX_DELAY = TDuration::Seconds(10);
        static constexpr size_t RETRY_MAX_COUNT = 5;
        static constexpr TDuration DATA_GC_MIN_AGE = TDuration::Hours(2);
        static constexpr ui64 DATA_GC_DATA_SIZE_PER_JOB = 3_GB;
        static constexpr i64 DATA_GC_DATA_SIZE_PER_KEY_RANGE = 1_TB;

        using TRetryPolicy = IRetryPolicy<const yexception&>;
        using TRetryPolicyPtr = TRetryPolicy::TPtr;
        using TOnFailFunc = std::function<void(const yexception&)>;

        struct TMainCluster {
            TString Proxy;
            NYT::TYPath DataDir;
            NYT::IClientPtr Client;
            bool Replicated{};
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
            ui64 End;

            friend IOutputStream& operator<<(IOutputStream& stream, const TKeyRange& self) {
                return stream << self.Begin << "-" << self.End;
            }

            NYT::TReadRange AsReadRange() const {
                return NYT::TReadRange::FromKeys(NYT::TNode(Begin), NYT::TNode(End));
            }
        };

    private:
        template <class TResult>
        auto WithRetry(std::function<TResult()> func, TOnFailFunc onFail) -> decltype(func()) {
            return std::move(*DoWithRetry(func, RetryPolicy_, true, onFail));
        }

        template <>
        void WithRetry(std::function<void()> func, TOnFailFunc onFail) {
            DoWithRetry(func, RetryPolicy_, true, onFail);
        }

        template <class TFunc>
        auto RetryUntilDisabled(TFunc func) -> decltype(func()) {
            try {
                return WithRetry(std::function(func), [this](const yexception&) {if (Disabled()) throw;});
            } catch (const yexception& e) {
                Disable(e);
                throw;
            }
        }

        template <class TFunc>
        auto RetryUntilCancelled(TFunc func, NThreading::TCancellationToken cToken) ->decltype(func()) {
            return WithRetry(std::function(func), [this, cToken](const yexception&) {if (cToken.IsCancellationRequested()) throw;});
        }

        NYT::IClientPtr ConnectToCluster(TString proxy, bool defaultRetryPolicy = false) {
            auto options = TCreateClientOptions().Token(Token_);
            if (!defaultRetryPolicy) {
                NYT::TConfigPtr cfg = MakeIntrusive<NYT::TConfig>();
                cfg->RetryCount = 1;
                options.Config(cfg);
            }
            return CreateClient(proxy, options);
        }

        template <class TOpts>
        requires (std::is_base_of_v<TTabletTransactionOptions<TOpts>, TOpts>)
        TOpts MakeTransactionOpts() {
            return TOpts().Atomicity(Atomicity_).Durability(Durability_).RequireSyncReplica(RequireSyncReplica_);
        }

        template <class TSpec>
        void AddPool(TSpec& spec) {
            if (OperationPool_) {
                spec.Pool(OperationPool_);
            }
        }

        void Disable(const yexception& err) {
            bool oldVal = Disabled_.exchange(true, std::memory_order::relaxed);
            if (!oldVal && OnDisable_) {
                YaYtStoreDisableHook(OnDisable_, TypeName(err), err.what());
            }
        }

        void Configure() {
            NYT::TNode attrs = RetryUntilDisabled([&]() {
                auto getOpts = NYT::TGetOptions()
                    .AttributeFilter(TAttributeFilter().Attributes({"type", "schema", "atomicity", "replicas"}))
                    .ReadFrom(EMasterReadKind::Cache);
                return MainCluster_.Client->Get(NYT::JoinYPaths(MainCluster_.DataDir, METADATA_TABLE, "@"), getOpts);
            });
            MetadataSchema_ = attrs.At("schema");
            if (attrs.At("atomicity") == "none") {
                Atomicity_ = NYT::EAtomicity::None;
            } else {
                // The async durability is not allowed for the full atomicity
                Durability_ = NYT::EDurability::Sync;
            }
            if (attrs.At("type") == "replicated_table") {
                MainCluster_.Replicated = true;
                for (const auto& [_, replicaInfo] : attrs.At("replicas").AsMap()) {
                    const TString& clusterName = replicaInfo.ChildAsString("cluster_name");
                    const TString& tablePath = replicaInfo.ChildAsString("replica_path");
                    const TString& stateStr = replicaInfo.ChildAsString("state");
                    const TString& modeStr = replicaInfo.ChildAsString("mode");
                    const TDuration lag = TDuration::MilliSeconds(replicaInfo.ChildAsInt64("replication_lag_time"));
                    const TStringBuf dataDir = TStringBuf{tablePath}.RSplitOff('/');

                    bool isGood = stateStr == "enabled";
                    bool syncReplica = modeStr == "sync";

                    if (syncReplica) {
                        // if we have at least one sync replica force to use it
                        RequireSyncReplica_ = true;
                    }

                    DEBUG_LOG << (isGood ? "Use" : "Ignore disabled") << " replica: " <<
                        "proxy=" << clusterName <<
                        ", dir=" << dataDir <<
                        ", state=" << stateStr <<
                        ", mode=" <<  modeStr <<
                        ", lag=" << lag.SecondsFloat();

                    if (isGood) {
                        Replicas_.push_back({
                            .Proxy = clusterName,
                            .DataDir = NYT::TYPath{dataDir},
                            .Client = ConnectToCluster(clusterName),
                            .Sync = syncReplica,
                            .Lag = lag
                        });
                    }
                }
                if (Replicas_.empty()) {
                    ythrow yexception() << "No enabled replica is found";
                }
            }
        }

        void Check() {
            // TODO Check cache availability to fail early
        }

        void Initialize(NThreading::TPromise<void> promise) {
            try {
                Configure();
                Check();
                promise.SetValue();
            } catch (const yexception& e) {
                Disable(e);
                promise.SetException(std::current_exception());
            }
        }

        std::pair<IClientPtr, TYPath> GetOpCluster() {
            if (MainCluster_.Replicated) {
                TDuration minLag = TDuration::Max();
                const TReplica* bestReplica{};
                for (const TReplica& r : Replicas_) {
                    if (r.Sync) {
                        bestReplica = &r;
                        break;
                    }
                    if (r.Lag < minLag) {
                        minLag = r.Lag;
                        bestReplica = &r;
                    }
                }
                DEBUG_LOG << "Operation replica: " << bestReplica->Proxy << ":" << bestReplica->DataDir;
                // Note: for operations we use separate client with default retry policy
                return std::make_pair(ConnectToCluster(bestReplica->Proxy, true), bestReplica->DataDir);
            } else {
                // Note: for operations we use separate client with default retry policy
                return std::make_pair(ConnectToCluster(MainCluster_.Proxy, true), MainCluster_.DataDir);
            }
        }

        size_t GetQuotaSize(IClientPtr client, const TYPath& dataDir) {
            NYT::TNode tableAttrs = RetryUntilDisabled([&]{
                auto getOpts = NYT::TGetOptions()
                    .AttributeFilter(TAttributeFilter().Attributes({"primary_medium", "account"}))
                    .ReadFrom(EMasterReadKind::Cache);
                return client->Get(NYT::JoinYPaths(dataDir, DATA_TABLE, "@"), getOpts);
            });
            const TString& accountName = tableAttrs.ChildAsString("account");
            const TString& primaryMedium = tableAttrs.ChildAsString("primary_medium");
            const NYT::TNode limits = RetryUntilDisabled([&] {
                return client->Get(
                    NYT::JoinYPaths("//sys/accounts/", accountName, "@resource_limits/disk_space_per_medium"),
                    NYT::TGetOptions().ReadFrom(EMasterReadKind::Cache)
                );
            });
            return limits.ChildAsInt64(primaryMedium);
        }

        size_t GetMaxCacheSize(IClientPtr client, const TYPath& dataDir) {
            if (std::holds_alternative<size_t>(MaxCacheSize_)) {
                return std::get<size_t>(MaxCacheSize_);
            }
            auto perc = std::get<double>(MaxCacheSize_);
            Y_ENSURE(perc > 0 && perc <= 100, "Wrong max cache size value");
            size_t quotaSize = GetQuotaSize(client, dataDir);
            return quotaSize * perc / 100;
        }

        size_t GetTablesSize(IClientPtr client, const TYPath& dataDir) {
            size_t total = 0;
            for (const TYPath& tableName : {METADATA_TABLE, DATA_TABLE}) {
                const NYT::TNode space = RetryUntilDisabled([&] {
                    return client->Get(
                        NYT::JoinYPaths(dataDir, tableName, "@resource_usage", "disk_space"),
                        TGetOptions().ReadFrom(EMasterReadKind::Cache)
                    );
                });
                total += space.AsInt64();
            }
            return total;
        }

        static inline void AddDataKeys(NYT::TNode::TListType& keys, const TString& hash, size_t chunks_count) {
            for (size_t i = 0; i < chunks_count; ++i) {
                keys.push_back(NYT::TNode()("hash", hash)("chunk_i", i));
            }
        }

        void DeleteRows(const NYT::TNode::TListType& deleteMetaKeys, const NYT::TNode::TListType& deleteDataKeys) {
            Y_ENSURE(!ReadOnly_);
            auto opts = MakeTransactionOpts<TDeleteRowsOptions>();
            if (!deleteMetaKeys.empty()) {
                RetryUntilDisabled([&] {
                    MainCluster_.Client->DeleteRows(NYT::JoinYPaths(MainCluster_.DataDir, METADATA_TABLE), deleteMetaKeys, opts);

                });
            }
            if (!deleteDataKeys.empty()) {
                RetryUntilDisabled([&] {
                    MainCluster_.Client->DeleteRows(NYT::JoinYPaths(MainCluster_.DataDir, DATA_TABLE), deleteDataKeys, opts);
                });
            }
        }

        void PutStat(const TString& key, const NYT::TNode& value) {
            Y_ENSURE(!ReadOnly_);
            NYT::TYPath statTable = NYT::JoinYPaths(MainCluster_.DataDir, STAT_TABLE);
            bool dynamic = MainCluster_.Client->Get(JoinYPaths(statTable, "@dynamic")).AsBool();
            auto row = NYT::TNode()("timestamp", ToYtTimestamp(TInstant::Now()))("key", key)("value", value);
            if (dynamic) {
                row["salt"] = RandomNumber<ui64>();
                RetryUntilDisabled([&] {
                    MainCluster_.Client->InsertRows(statTable, {row}, MakeTransactionOpts<TInsertRowsOptions>());
                });
            } else {
                NYT::TRichYPath richPath = NYT::TRichYPath(statTable).Append(true);
                RetryUntilDisabled([&] {
                    auto writer = MainCluster_.Client->CreateTableWriter<NYT::TNode>(richPath);
                    writer->AddRow(row);
                    writer->Finish();
                });
            }
        }

    private:
        TMainCluster MainCluster_{};
        TVector<TReplica> Replicas_{};
        TString Token_;
        bool ReadOnly_;
        void* OnDisable_;
        TMaxCacheSize MaxCacheSize_;
        TDuration Ttl_;
        TVector<TNameReTtl> NameReTtls_;
        TString OperationPool_;
        NYT::EDurability Durability_;
        NYT::EAtomicity Atomicity_{NYT::EAtomicity::Full};
        bool RequireSyncReplica_{false};

        std::atomic_bool Disabled_{};
        TAdaptiveThreadPool ThreadPool_{};
        NThreading::TFuture<void> InitializeFuture_{};
        TRetryPolicyPtr RetryPolicy_;
        NYT::TNode MetadataSchema_{};
    };

    TYtStore2::TYtStore2(const TString& proxy, const TString& dataDir, const TYtStore2Options& options) {
        Impl_ = std::make_unique<TImpl>(proxy, dataDir, options);
    }

    TYtStore2::~TYtStore2() = default;

    void TYtStore2::WaitInitialized() {
        Impl_->WaitInitialized();
    }

    bool TYtStore2::Disabled() const {
        return Impl_->Disabled();
    }

    void TYtStore2::Strip() {
        Impl_->Strip();
    }

    void TYtStore2::DataGc() {
        Impl_->DataGc();
    }

    void TYtStore2::ValidateRegexp(const TString& re) {
        TImpl::ValidateRegexp(re);
    }
}
