#include "service.h"

#include "devtools/local_cache/ac/fs/fs_blobs.h"

#include "devtools/local_cache/psingleton/server/config.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/folder/path.h>
#include <util/generic/scope.h>
#include <util/string/cast.h>
#include <util/system/fs.h>

namespace {
    using namespace grpc;
    // TODO: duplicate code.
    ClientContext& SetDeadline(ClientContext& ctx, int deadline) {
        if (deadline != INT_MAX && deadline != 0) {
            ctx.set_wait_for_ready(true);
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline));
        }
        return ctx;
    }
}

namespace NACCache {
    using namespace grpc;
    using namespace NConfig;
    using namespace NACCachePrivate;

    TACCacheServer::TACCacheServer(const NConfig::TConfig& config, TLog& log, const TCriticalErrorHandler& handler, const NUserService::IServer* server)
        : ErrorHandler_(handler)
        , Server_(server)
        , Config_(config)
        , Log_(log)
        , QuiescenceTime_(0)
        , PollMode_(PollAll)
        , MasterMode_(false)
        , SharedMode_(NUserServicePrivate::IsSharedMode(config))
        , GraphInfo_(false)
        , ForeignKeys_(false)
        , NoDb_(false)
        , NoBlobIO_(false)
        , Recreate_(false)
        , HasStats_("Has")
        , PutStats_("Put")
        , GetStats_("Get")
        , RemoveStats_("Remove")
        , PutDepsStats_("PutDeps")
    {
        SetSharedMode(SharedMode_);
        const auto& acSection = GetSection();
        MasterMode_ = acSection.contains(ToString(MasterModeStr)) ? acSection.At(ToString(MasterModeStr)).As<bool>() : false;
        GraphInfo_ = acSection.contains(ToString(GraphInfoStr)) ? acSection.At(ToString(GraphInfoStr)).As<bool>() : false;
        ForeignKeys_ = acSection.contains(ToString(ForeignKeysStr)) ? acSection.At(ToString(ForeignKeysStr)).As<bool>() : false;
        NoDb_ = acSection.contains(ToString(NoDbStr)) ? acSection.At(ToString(NoDbStr)).As<bool>() : false;
        NoBlobIO_ = acSection.contains(ToString(NoBlobIOStr)) ? acSection.At(ToString(NoBlobIOStr)).As<bool>() : false;
        Recreate_ = acSection.contains(ToString(RecreateStr)) ? acSection.At(ToString(RecreateStr)).As<bool>() : false;
        PollMode_ = NoDb_ && acSection.contains(ToString(PollStr))
                        ? EPollersConf::FromBaseType(acSection.At(ToString(PollStr)).As<int>())
                        : PollAll;
    }

    TACCacheServer::~TACCacheServer() {
        HasStats_.PrintSummary(Log_);
        PutStats_.PrintSummary(Log_);
        GetStats_.PrintSummary(Log_);
        RemoveStats_.PrintSummary(Log_);
        PutDepsStats_.PrintSummary(Log_);
        StopProcessing();
    }

    const NConfig::TDict& TACCacheServer::GetSection() const {
        return Config_.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(BuildCacheStr)).Get<TDict>();
    }

    void TACCacheServer::StartProcessing() {
        TWriteGuard lock(DBBEMutex_);
        if (DBBE_) {
            return;
        }

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Prepare processing..." << Endl;
        Log_.ReopenLog();

        const auto& acSection = GetSection();
        {
            const auto& storeDir = acSection.At(ToString(FsStoreStr)).Get<TString>();
            const auto& dbPath = acSection.contains(ToString(DbPathStr)) ? acSection.At(ToString(DbPathStr)).Get<TString>() : JoinFsPaths("file:" + storeDir, "acdb.sqlite");
            auto procMaxQueue = acSection.contains(ToString(RunningMaxQueueStr)) ? acSection.At(ToString(RunningMaxQueueStr)).As<int>() : 0;
            auto gcMaxQueue = acSection.contains(ToString(GcMaxQueueStr)) ? acSection.At(ToString(GcMaxQueueStr)).As<int>() : 0;
            auto gcLimit = acSection.contains(ToString(GcLimitStr)) ? acSection.At(ToString(GcLimitStr)).As<i64>() : 20LL * 1024LL * 1024LL * 1024LL;
            auto casLogging = acSection.contains(ToString(CasLoggingStr)) ? acSection.At(ToString(CasLoggingStr)).As<bool>() : false;
            QuiescenceTime_ = acSection.contains(ToString(QuiescenceStr)) ? acSection.At(ToString(QuiescenceStr)).As<int>() : 10 * 1000;
            TCodecConfig codec;
            if (acSection.contains(ToString(CodecStr))) {
                codec.Codec = CodecFromName(acSection.At(ToString(CodecStr)).Get<TString>());
            }
            codec.MinSize = acSection.contains(ToString(CodecMinSizeStr)) ? acSection.At(ToString(CodecMinSizeStr)).As<i64>() : codec.MinSize;
            codec.CompressResults = acSection.contains(ToString(CodecResultsStr)) ? acSection.At(ToString(CodecResultsStr)).As<bool>() : codec.CompressResults;
            codec.CompressOnPut = acSection.contains(ToString(CodecOnPutStr)) ? acSection.At(ToString(CodecOnPutStr)).As<bool>() : codec.CompressOnPut;
            codec.PromoteOnGet = acSection.contains(ToString(CodecPromoteOnGetStr)) ? acSection.At(ToString(CodecPromoteOnGetStr)).As<bool>() : codec.PromoteOnGet;
            codec.MaxParallelEncode = acSection.contains(ToString(CodecMaxParallelStr)) ? acSection.At(ToString(CodecMaxParallelStr)).As<int>() : codec.MaxParallelEncode;
            codec.ColdAge = acSection.contains(ToString(CodecColdAgeStr)) ? acSection.At(ToString(CodecColdAgeStr)).As<i64>() : codec.ColdAge;
            codec.ColdInterval = acSection.contains(ToString(CodecColdIntervalStr)) ? acSection.At(ToString(CodecColdIntervalStr)).As<i64>() : codec.ColdInterval;
            codec.ColdBatch = acSection.contains(ToString(CodecColdBatchStr)) ? acSection.At(ToString(CodecColdBatchStr)).As<int>() : codec.ColdBatch;
            auto diskLimitKind = acSection.contains(ToString(DiskLimitKindStr)) ? acSection.At(ToString(DiskLimitKindStr)).Get<TString>() : TString("fs");

            auto marker = GetCriticalErrorMarkerFileName(Config_);
            if (NFs::Exists(marker)) {
                Recreate_ = true;
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Going to recreate DB and eliminate files due to file marker for critical error: "
                                                                                                 << marker << Endl;
            }

            Recreate_ = CreateDBIfNeeded(dbPath, Log_, Recreate_);
            DBBE_.Reset(new TACCacheDBBE(dbPath, NoBlobIO_, ForeignKeys_, gcLimit, Log_, ErrorHandler_, storeDir, procMaxQueue, gcMaxQueue, QuiescenceTime_, casLogging, Recreate_, codec, diskLimitKind == "content" ? EDiskLimitKind::Content : EDiskLimitKind::Fs));
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "BE created for sqlite db: " << dbPath << Endl;
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "BE parameters:"
                                                                                             << " DB file - " << dbPath
                                                                                             << ", bytes to keep - " << gcLimit
                                                                                             << ", max queue sizes - " << procMaxQueue << " " << gcMaxQueue
                                                                                             << ", no_db - " << NoDb_
                                                                                             << ", no_blob_io - " << NoBlobIO_
                                                                                             << ", poll - " << PollMode_
                                                                                             << ", quiescence time - " << QuiescenceTime_
                                                                                             << ", graph_info - " << GraphInfo_
                                                                                             << ", cache_dir - " << storeDir
                                                                                             << ", cas_logging - " << casLogging
                                                                                             << ", foreign_keys - " << ForeignKeys_
                                                                                             << ", recreate_db - " << Recreate_
                                                                                             << ", codec - '" << CodecName(codec.Codec) << "'"
                                                                                             << ", codec_min_size - " << codec.MinSize
                                                                                             << ", codec_results - " << codec.CompressResults
                                                                                             << ", codec_on_put - " << codec.CompressOnPut
                                                                                             << ", codec_promote_on_get - " << codec.PromoteOnGet
                                                                                             << ", codec_max_parallel - " << codec.MaxParallelEncode
                                                                                             << ", codec_cold_age - " << codec.ColdAge
                                                                                             << ", codec_cold_interval - " << codec.ColdInterval
                                                                                             << ", codec_cold_batch - " << codec.ColdBatch
                                                                                             << ", disk_limit_kind - " << diskLimitKind
                                                                                             << Endl;
            if (NFs::Exists(marker)) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Successfully recreated AC cache" << Endl;
                NFs::Remove(marker);
            }

            if (NFs::Exists(marker)) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_EMERG, "EMERG[ACSERV]") << "Cannot remove marker file: " << marker << Endl;
            }
        }

        // Start threads in process polling and gc/fs handling
        DBBE_->Initialize(PollMode_);
        DBBE_->SetMasterMode(MasterMode_);
        if (acSection.contains(ToString(DistCacheUrlStr))) {
            GetAnnouncer(acSection.At(ToString(DistCacheUrlStr)).Get<TString>());
        }
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Set master mode: " << MasterMode_ << Endl;
    }

    void TACCacheServer::StopProcessing() {
        THolder<NDistCache::TAnnouncer> announcer;
        {
            TWriteGuard lock(DBBEMutex_);
            if (!DBBE_) {
                return;
            }

            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Set non-master mode: " << false << Endl;
            DBBE_->SetMasterMode(false);
            DBBE_->Finalize();
            DBBE_->Flush();

            {
                std::lock_guard announcerLock(AnnouncerMutex_);
                announcer = std::move(Announcer_);
            }
            DBBE_.Reset(nullptr);
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Stopped BE" << Endl;
            Log_.ReopenLog();
        }
        // No Put can retain this announcer after the write guard above.
        // Wait for telemetry without blocking cache RPCs on network drain.
        if (announcer) {
            announcer->Stop();
        }
    }

    bool TACCacheServer::IsQuiescent() const noexcept {
        if (SharedMode_) {
            return false;
        }
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return false;
        }
        {
            std::lock_guard announcerLock(AnnouncerMutex_);
            if (Announcer_ && !Announcer_->IsIdle()) {
                return false;
            }
        }
        return NoDb_
                   ? MilliSeconds() >= static_cast<ui64>(AtomicGet(LastAccessTime_)) + static_cast<ui64>(QuiescenceTime_)
                   : DBBE_->IsQuiescent();
    }

    int TACCacheServer::PollingDelay() const noexcept {
        return QuiescenceTime_;
    }

    void TACCacheServer::SetMasterMode(bool master) {
        TWriteGuard lock(DBBEMutex_);
        MasterMode_ = master;
        if (!DBBE_) {
            return;
        }

        DBBE_->SetMasterMode(master);
    }

    bool TACCacheServer::GetMasterMode() const noexcept {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return false;
        }

        return DBBE_->GetMasterMode();
    }

    // TODO: duplicate code, see devtools/local_cache/psingleton/service/service.cpp
    Status TACCacheServer::SetStatus(TStringBuf method, ServerContext* context) const {
        if (context->IsCancelled()) {
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[ACSERV]") << "Cancelled request" << Endl;
            return Status(StatusCode::CANCELLED, ToString(method) + ": deadline exceeded or client cancelled.");
        }
        if (Recreate_) {
            context->AddTrailingMetadata("ac-db-recreated", "true");
        }
        return Status::OK;
    }

    void TACCacheServer::RemoveOnFailure(const THash& hash, const TSystemError& e, grpc::ServerContext* context, TACStatus* response) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[ACSERV]")
            << "Unexpected exception(" << e.what() << ") for uid: " << hash << Endl;
        TRemoveUid remRequest;
        remRequest.SetForcedRemoval(true);
        *remRequest.MutableACHash() = hash;
        DBBE_->RemoveUid(remRequest);

        response->SetSuccess(false);
        response->SetOptim(Hardlink);
        context->AddTrailingMetadata("io-exception", e.what());
    }

    void TACCacheServer::ReportRestoreFailure(const THash& hash, const TRestoreError& e, grpc::ServerContext* context, TACStatus* response) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_WARNING, "WARN[ACSERV]")
            << "Failed to restore uid " << hash << " into the client destination: " << e.what() << Endl;
        response->SetSuccess(false);
        response->SetOptim(Copy);
        context->AddTrailingMetadata("io-exception", e.what());
    }

    NDistCache::TAnnouncer* TACCacheServer::GetAnnouncer(const TString& url) noexcept {
        if (url.empty() || SharedMode_ || NoDb_ || NoBlobIO_) {
            return nullptr;
        }
        std::lock_guard lock(AnnouncerMutex_);
        if (AnnouncerFailed_) {
            return nullptr;
        }
        try {
            TString normalized = url;
            while (normalized.EndsWith('/')) {
                normalized.pop_back();
            }
            if (Announcer_) {
                if (Announcer_->Url() == normalized) {
                    return Announcer_.Get();
                }
                if (!AnnouncerUrlMismatchLogged_) {
                    LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_WARNING, "WARN[DIST-CACHE]")
                        << "Different telemetry endpoint requested; restart ya-tc to change endpoint" << Endl;
                    AnnouncerUrlMismatchLogged_ = true;
                }
                return nullptr;
            }
            const auto& section = GetSection();
            auto value = [&section](EConfigStrings key, i64 fallback) {
                const auto result = section.contains(ToString(key)) ? section.At(ToString(key)).As<i64>() : fallback;
                Y_ENSURE(result > 0, "dist-cache limits must be positive");
                return result;
            };
            NDistCache::TOptions options;
            options.Url = normalized;
            options.CacheDir = section.At(ToString(FsStoreStr)).Get<TString>();
            options.BatchSize = value(DistCacheBatchSizeStr, options.BatchSize);
            options.MaxQueueItems = value(DistCacheQueueItemsStr, options.MaxQueueItems);
            options.MaxQueueBytes = value(DistCacheQueueBytesStr, options.MaxQueueBytes);
            options.FlushInterval = TDuration::MilliSeconds(value(DistCacheFlushMsStr, options.FlushInterval.MilliSeconds()));
            options.Cooldown = TDuration::MilliSeconds(value(DistCacheCooldownMsStr, options.Cooldown.MilliSeconds()));
            options.RequestTimeout = TDuration::MilliSeconds(value(DistCacheTimeoutMsStr, options.RequestTimeout.MilliSeconds()));
            options.DrainTimeout = TDuration::MilliSeconds(value(DistCacheDrainMsStr, options.DrainTimeout.MilliSeconds()));
            Announcer_ = MakeHolder<NDistCache::TAnnouncer>(std::move(options), Log_);
            return Announcer_.Get();
        } catch (...) {
            AnnouncerFailed_ = true;
            try {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_WARNING, "WARN[DIST-CACHE]")
                    << "Cannot initialize telemetry; cache operations remain enabled: " << CurrentExceptionMessage() << Endl;
            } catch (...) {
            }
            return nullptr;
        }
    }

    Status TACCacheServer::Put(ServerContext* context, const TPutUid* request, TACStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("Put"));
        }

        auto start = TInstant::Now();
        Y_DEFER {
            auto end = TInstant::Now();
            auto& uid = request->GetACHash().GetUid();
            PutStats_.UpdateStats(end - start, uid, Log_);
        };

        response->Clear();
        response->SetSuccess(false);
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                try {
                    // A reused daemon may receive its first opted-in Put after startup.
                    // Reuse the announcer only for requests opting in to the same URL.
                    auto* announcer = GetAnnouncer(request->GetDistCacheUrl());
                    TPutUid resolved;
                    auto r = DBBE_->PutUid(*request, announcer ? &resolved : nullptr);
                    if (announcer && !resolved.GetACHash().GetUid().empty()) {
                        announcer->Enqueue(std::move(resolved));
                    }
                    response->SetSuccess(r.Success);
                    response->SetOptim(r.CopyMode);
                } catch (const TSystemError& e) {
                    RemoveOnFailure(request->GetACHash(), e, context, response);
                }
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response->MutableStats());
        });
        return SetStatus("Put", context);
    }

    Status TACCacheServer::Get(ServerContext* context, const TGetUid* request, TACStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("Get"));
        }

        auto start = TInstant::Now();
        Y_DEFER {
            auto end = TInstant::Now();
            auto& uid = request->GetACHash().GetUid();
            GetStats_.UpdateStats(end - start, uid, Log_);
        };

        response->Clear();
        response->SetSuccess(false);
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                try {
                    auto r = DBBE_->GetUid(*request);
                    response->SetSuccess(r.Success);
                    response->SetOptim(r.CopyMode);
                } catch (const TRestoreError& e) {
                    ReportRestoreFailure(request->GetACHash(), e, context, response);
                } catch (const TSystemError& e) {
                    RemoveOnFailure(request->GetACHash(), e, context, response);
                }
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response->MutableStats());
        });
        return SetStatus("Get", context);
    }

    Status TACCacheServer::Remove(ServerContext* context, const TRemoveUid* request, TACStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("Remove"));
        }

        auto start = TInstant::Now();
        Y_DEFER {
            auto end = TInstant::Now();
            auto& uid = request->GetACHash().GetUid();
            RemoveStats_.UpdateStats(end - start, uid, Log_);
        };

        response->Clear();
        response->SetSuccess(false);
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                auto r = DBBE_->RemoveUid(*request);
                response->SetSuccess(r.Success);
                response->SetOptim(r.CopyMode);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response->MutableStats());
        });
        return SetStatus("Remove", context);
    }

    Status TACCacheServer::Has(ServerContext* context, const THasUid* request, TACStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("Has"));
        }

        auto start = TInstant::Now();
        Y_DEFER {
            auto end = TInstant::Now();
            auto& uid = request->GetACHash().GetUid();
            HasStats_.UpdateStats(end - start, uid, Log_);
        };

        response->Clear();
        response->SetSuccess(false);
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                auto r = DBBE_->HasUid(*request);
                response->SetSuccess(r.Success);
                response->SetOptim(r.CopyMode);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response->MutableStats());
        });
        return SetStatus("Has", context);
    }

    Status TACCacheServer::ForceGC(ServerContext* context, const TForceGC* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("ForceGC"));
        }

        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->ForceGC(*request);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response);
        });
        return SetStatus("ForceGC", context);
    }

    Status TACCacheServer::GetTaskStats(ServerContext* context, const NUserService::TPeer* request, TTaskStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("GetTaskStats"));
        }

        auto start = TInstant::Now();
        Y_DEFER {
            auto end = TInstant::Now();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[AC]") << "GetTaskStats time: " << end - start << Endl;
        };

        HasStats_.PrintAndReset(Log_);
        PutStats_.PrintAndReset(Log_);
        GetStats_.PrintAndReset(Log_);
        RemoveStats_.PrintAndReset(Log_);
        PutDepsStats_.PrintAndReset(Log_);

        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->GetTaskStats(*request, response);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
        });
        return SetStatus("GetTaskStats", context);
    }

    Status TACCacheServer::PutDeps(grpc::ServerContext* context, const TNodeDependencies* request, TACStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("PutDeps"));
        }

        auto start = TInstant::Now();
        Y_DEFER {
            auto end = TInstant::Now();
            auto& uid = request->GetNodeHash().GetUid();
            PutDepsStats_.UpdateStats(end - start, uid, Log_);
        };

        response->Clear();
        response->SetSuccess(false);
        if (!GraphInfo_) {
            DBBE_->GetStats(response->MutableStats());
            return SetStatus("PutDeps", context);
        }
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                response->SetSuccess(DBBE_->PutDeps(*request));
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response->MutableStats());
        });
        return SetStatus("PutDeps", context);
    }

    Status TACCacheServer::ReleaseAll(ServerContext* context, const NUserService::TPeer* request, TTaskStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("ReleaseAll"));
        }

        HasStats_.PrintAndReset(Log_);
        PutStats_.PrintAndReset(Log_);
        GetStats_.PrintAndReset(Log_);
        RemoveStats_.PrintAndReset(Log_);
        PutDepsStats_.PrintAndReset(Log_);

        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                // TODO: speed-up
                // DBBE_->GetTaskStats(*request, response);
                DBBE_->ReleaseAll(*request);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
        });
        return SetStatus("ReleaseAll", context);
    }

    Status TACCacheServer::GetCacheStats(grpc::ServerContext* context, const TStatus* request, grpc::ServerWriter<TStatus>* writer) {
        if (NoDb_) {
            return Status::OK;
        }

        TStatus lastOut(*request);
        bool sentSomething = false;
        while (!context->IsCancelled() && Server_->GetState() != NUserService::ShuttingDown) {
            TStatus response;
            {
                TReadGuard lock(DBBEMutex_);
                if (!DBBE_) {
                    return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("GetCacheStats"));
                }
                DBBE_->GetStats(&response);
            }
            if (!sentSomething || response.GetTotalSize() != lastOut.GetTotalSize() || response.GetTotalFSSize() != lastOut.GetTotalFSSize()) {
                writer->Write(response, ::grpc::WriteOptions());
                sentSomething = true;
            }
            lastOut = response;
            gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_millis(1000, GPR_TIMESPAN)));
        }
        return SetStatus("GetCacheStats", context);
    }

    Status TACCacheServer::AnalyzeDU(ServerContext* context, const TStatus*, TDiskUsageSummary* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("AnalyzeDU"));
        }

        HasStats_.PrintAndReset(Log_);
        PutStats_.PrintAndReset(Log_);
        GetStats_.PrintAndReset(Log_);
        RemoveStats_.PrintAndReset(Log_);
        PutDepsStats_.PrintAndReset(Log_);

        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->GetStats(response->MutableStats());
                DBBE_->AnalyzeDU(response);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
        });
        return SetStatus("AnalyzeDU", context);
    }

    Status TACCacheServer::SynchronousGC(ServerContext* context, const TSyncronousGC* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("SynchronousGC"));
        }

        HasStats_.PrintAndReset(Log_);
        PutStats_.PrintAndReset(Log_);
        GetStats_.PrintAndReset(Log_);
        RemoveStats_.PrintAndReset(Log_);
        PutDepsStats_.PrintAndReset(Log_);

        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->SynchronousGC(*request);
                DBBE_->GetStats(response);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
        });
        return SetStatus("SynchronousGC", context);
    }

    std::string TACCacheServer::ErrorMessage(const char* method) const {
        return std::string(method) + ": server is not processing reqs. Master=" + (MasterMode_ ? "true" : "false");
    }

    // Should hold lock in caller.
    TAtomic TACCacheServer::LastAccessTime_;
}

namespace NACCache {
    TStatus TACCacheClient::ForceGC(i64 targetSize) {
        TForceGC req;
        req.SetTargetSize(targetSize);
        GetMyName("TAClient", req.MutablePeer(), NUserService::RandomizeName);

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->ForceGC(&SetDeadline(ctx, Deadline_), req, &state);
        if (r.ok()) {
            return state;
        }
        ythrow NUserService::TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }
}
