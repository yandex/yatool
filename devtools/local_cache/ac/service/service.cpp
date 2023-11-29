#include "service.h"

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

            auto marker = GetCriticalErrorMarkerFileName(Config_);
            if (NFs::Exists(marker)) {
                Recreate_ = true;
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Going to recreate DB and eliminate files due to file marker for critical error: "
                                                                                                 << marker << Endl;
            }

            Recreate_ = CreateDBIfNeeded(dbPath, Log_, Recreate_);
            DBBE_.Reset(new TACCacheDBBE(dbPath, NoBlobIO_, ForeignKeys_, gcLimit, Log_, ErrorHandler_, storeDir, procMaxQueue, gcMaxQueue, QuiescenceTime_, casLogging, Recreate_));
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
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Set master mode: " << MasterMode_ << Endl;
    }

    void TACCacheServer::StopProcessing() {
        TWriteGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return;
        }

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Set non-master mode: " << false << Endl;
        DBBE_->SetMasterMode(false);
        DBBE_->Finalize();
        DBBE_->Flush();

        DBBE_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[ACSERV]") << "Stopped BE" << Endl;
        Log_.ReopenLog();
    }

    bool TACCacheServer::IsQuiescent() const noexcept {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_) {
            return false;
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
                    auto r = DBBE_->PutUid(*request);
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
            if (!sentSomething || response.GetTotalSize() != lastOut.GetTotalSize()) {
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
