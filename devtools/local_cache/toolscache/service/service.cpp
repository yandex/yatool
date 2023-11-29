#include "service.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <util/string/cast.h>

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

    std::string ErrorMessage(const char* method, bool master) {
        return std::string(method) + ": server is not processing reqs. Master=" + (master ? "true" : "false");
    }
}

namespace NToolsCache {
    using namespace grpc;
    using namespace NConfig;
    using namespace NToolsCachePrivate;

    TToolsCacheServer::TToolsCacheServer(const NConfig::TConfig& config, TLog& log, const TCriticalErrorHandler& handler, const NUserService::IServer*)
        : ErrorHandler_(handler)
        , Config_(config)
        , Log_(log)
        , QuiescenceTime_(0)
        , PollMode_(PollAll)
        , MasterMode_(false)
        , NoDb_(false)
    {
        const auto& tcSection = GetSection(Config_);
        MasterMode_ = tcSection.contains(ToString(MasterModeStr)) ? tcSection.At(ToString(MasterModeStr)).As<bool>() : false;
        NoDb_ = tcSection.contains(ToString(NoDbStr)) ? tcSection.At(ToString(NoDbStr)).As<bool>() : false;
        PollMode_ = NoDb_ && tcSection.contains(ToString(PollStr))
                        ? EPollersConf::FromBaseType(tcSection.At(ToString(PollStr)).As<int>())
                        : PollAll;
    }

    TToolsCacheServer::~TToolsCacheServer() {
        StopProcessing();
    }

    const NConfig::TDict& TToolsCacheServer::GetSection(const TConfig& config) {
        return config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(ToolsCacheStr)).Get<TDict>();
    }

    void TToolsCacheServer::StartProcessing() {
        TWriteGuard lock(DBBEMutex_);
        if (Pool_.Get()) {
            return;
        }

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Prepare processing..." << Endl;
        Log_.ReopenLog();
        THolder<IThreadPool> pool(CreateThreadPool(2));

        const auto& tcSection = GetSection(Config_);
        {
            const auto& dbPath = tcSection.At(ToString(DbPathStr)).Get<TString>();
            auto procMaxQueue = tcSection.contains(ToString(RunningMaxQueueStr)) ? tcSection.At(ToString(RunningMaxQueueStr)).As<int>() : 0;
            auto gcMaxQueue = tcSection.contains(ToString(GcMaxQueueStr)) ? tcSection.At(ToString(GcMaxQueueStr)).As<int>() : 0;
            auto gcLimit = tcSection.contains(ToString(GcLimitStr)) ? tcSection.At(ToString(GcLimitStr)).As<i64>() : 20LL * 1024LL * 1024LL * 1024LL;
            QuiescenceTime_ = tcSection.contains(ToString(QuiescenceStr)) ? tcSection.At(ToString(QuiescenceStr)).As<int>() : 10 * 1000;

            (void)CreateDBIfNeeded(dbPath, Log_);
            DBBE_.Reset(new TToolsCacheDBBE(dbPath, gcLimit, *pool, Log_, ErrorHandler_, procMaxQueue, gcMaxQueue, QuiescenceTime_));
            Pool_.Reset(pool.Release());
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "BE created for sqlite db: " << dbPath << Endl;
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "BE parameters:"
                                                                                             << " DB file - " << dbPath
                                                                                             << ", bytes to keep - " << gcLimit
                                                                                             << ", max queue sizes - " << procMaxQueue << " " << gcMaxQueue
                                                                                             << ", no_db - " << NoDb_
                                                                                             << ", poll - " << PollMode_
                                                                                             << ", quiescence time - " << QuiescenceTime_
                                                                                             << Endl;
        }

        InsertMyself(Config_, NoDb_ ? nullptr : DBBE_.Get(), nullptr);
        // Start threads in process polling and gc/fs handling
        DBBE_->Initialize(PollMode_);
        DBBE_->SetMasterMode(MasterMode_);
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Set master mode: " << MasterMode_ << Endl;
    }

    void TToolsCacheServer::StopProcessing() {
        TWriteGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return;
        }

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Set non-master mode: " << false << Endl;
        DBBE_->SetMasterMode(false);
        DBBE_->Finalize();
        DBBE_->Flush();

        DBBE_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Stopped DB" << Endl;
        Pool_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[TCSERV]") << "Stopped BE" << Endl;
        Log_.ReopenLog();
    }

    bool TToolsCacheServer::IsQuiescent() const noexcept {
        TReadGuard lock(DBBEMutex_);
        if (!DBBE_.Get()) {
            return false;
        }
        return NoDb_
                   ? MilliSeconds() >= static_cast<ui64>(AtomicGet(LastAccessTime_)) + static_cast<ui64>(QuiescenceTime_)
                   : DBBE_->IsQuiescent();
    }

    int TToolsCacheServer::PollingDelay() const noexcept {
        return QuiescenceTime_;
    }

    void TToolsCacheServer::SetMasterMode(bool master) {
        TWriteGuard lock(DBBEMutex_);
        MasterMode_ = master;
        if (!Pool_.Get()) {
            return;
        }

        DBBE_->SetMasterMode(master);
    }

    bool TToolsCacheServer::GetMasterMode() const noexcept {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return false;
        }

        return DBBE_->GetMasterMode();
    }

    // TODO: duplicate code, see devtools/local_cache/psingleton/service/service.cpp
    Status SetStatus(TStringBuf method, ServerContext* context, TLog& log) {
        if (context->IsCancelled()) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[TCSERV]") << "Cancelled request" << Endl;
            return Status(StatusCode::CANCELLED, ToString(method) + ": deadline exceeded or client cancelled.");
        }
        return Status::OK;
    }

    Status TToolsCacheServer::Notify(ServerContext* context, const TResourceUsed* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("Notify", MasterMode_));
        }
        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->InsertResource(*request);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response);
        });
        return SetStatus("Notify", context, Log_);
    }

    Status TToolsCacheServer::NotifyNewService(ServerContext* context, const TServiceStarted* request, TServiceResponse* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("NotifyNewService", MasterMode_));
        }
        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->InsertService(*request, response->MutableService());
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response->MutableStatus());
        });
        return SetStatus("NotifyNewService", context, Log_);
    }

    Status TToolsCacheServer::ForceGC(ServerContext* context, const TForceGC* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("ForceGC", MasterMode_));
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
        return SetStatus("ForceGC", context, Log_);
    }

    Status TToolsCacheServer::LockResource(ServerContext* context, const TSBResource* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("LockResource", MasterMode_));
        }
        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->LockResource(*request);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response);
        });
        return SetStatus("LockResource", context, Log_);
    }

    Status TToolsCacheServer::UnlockSBResource(ServerContext* context, const TSBResource* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("UnlockSBResource", MasterMode_));
        }
        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->UnlockSBResource(*request);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response);
        });
        return SetStatus("UnlockSBResource", context, Log_);
    }

    Status TToolsCacheServer::UnlockAllResources(ServerContext* context, const NUserService::TPeer* request, TStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("UnlockAllResources", MasterMode_));
        }
        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->UnlockAllResources(*request);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
            DBBE_->GetStats(response);
        });
        return SetStatus("UnlockAllResources", context, Log_);
    }

    Status TToolsCacheServer::GetTaskStats(ServerContext* context, const NUserService::TPeer* request, TTaskStatus* response) {
        TReadGuard lock(DBBEMutex_);
        if (!Pool_.Get()) {
            return Status(StatusCode::FAILED_PRECONDITION, ErrorMessage("GetTaskStats", MasterMode_));
        }
        response->Clear();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (!NoDb_) {
                DBBE_->GetTaskStats(*request, response);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
        });
        return SetStatus("GetTaskStats", context, Log_);
    }

    // Should hold lock in caller for dbbe, another instance hold lock for TToolsCacheClient
    // It is redundant to use InsertService here, InsertResource is sufficient.
    void TToolsCacheServer::InsertMyself(const TConfig& config, TToolsCacheDBBE* dbbe, TToolsCacheClient* client) {
        const auto& tcSection = GetSection(config);
        if (!tcSection.contains(ToString(VersionStr)) || !tcSection.contains(ToString(SandBoxPathStr))) {
            return;
        }

        auto version = tcSection.At(ToString(VersionStr)).As<int>();
        const auto& sbPath = tcSection.At(ToString(SandBoxPathStr)).Get<TString>();

        TServiceStarted req;
        GetMyName("IAM", req.MutablePeer(), NUserService::RandomizeName);
        req.MutableService()->SetName("ya-tc");
        req.MutableService()->SetEnvCwdArgs("");
        req.MutableService()->SetVersion(version);
        req.MutableService()->MutableResource()->SetPath(sbPath);

        for (auto e : {SandBoxIdStr, SandBoxAltIdStr}) {
            if (!tcSection.contains(ToString(e))) {
                continue;
            }

            const auto& sbId = tcSection.At(ToString(e)).Get<TString>();
            req.MutableService()->MutableResource()->SetSBId(sbId);

            TServiceInfo existingService;
            if (dbbe) {
                dbbe->InsertService(req, &existingService);
            } else if (client) {
                client->NotifyNewService(req);
            } else {
                AtomicSet(LastAccessTime_, MilliSeconds());
            }
        }
    }

    TAtomic TToolsCacheServer::LastAccessTime_;
}

namespace NToolsCache {
    TStatus TToolsCacheClient::ForceGC(i64 targetSize) {
        TForceGC req;
        req.SetTargetSize(targetSize);
        GetMyName("TToolsClient", req.MutablePeer(), NUserService::RandomizeName);

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->ForceGC(&SetDeadline(ctx, Deadline_), req, &state);
        if (r.ok()) {
            return state;
        }
        ythrow NUserService::TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    TStatus TToolsCacheClient::LockResource(const TSBResource& resource) {
        TStatus state;
        ClientContext ctx;
        auto r = Stub_->LockResource(&SetDeadline(ctx, Deadline_), resource, &state);
        if (r.ok()) {
            return state;
        }
        ythrow NUserService::TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    TStatus TToolsCacheClient::UnlockSBResource(const TSBResource& resource) {
        TStatus state;
        ClientContext ctx;
        auto r = Stub_->UnlockSBResource(&SetDeadline(ctx, Deadline_), resource, &state);
        if (r.ok()) {
            return state;
        }
        ythrow NUserService::TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    TStatus TToolsCacheClient::UnlockAllResources() {
        NUserService::TPeer request;
        GetMyName("TToolsClient", &request, NUserService::RandomizeName);

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->UnlockAllResources(&SetDeadline(ctx, Deadline_), request, &state);
        if (r.ok()) {
            return state;
        }
        ythrow NUserService::TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    TServiceResponse TToolsCacheClient::NotifyNewService(const TServiceStarted& service) {
        TServiceResponse state;
        ClientContext ctx;
        auto r = Stub_->NotifyNewService(&SetDeadline(ctx, Deadline_), service, &state);
        if (r.ok()) {
            return state;
        }
        ythrow NUserService::TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }
}
