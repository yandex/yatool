#include "server.h"
#include "config.h"

#include "devtools/local_cache/psingleton/server/server-nix.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>
#include <library/cpp/sqlite3/sqlite.h>

#include <util/generic/scope.h>
#include <util/system/tempfile.h>

#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server_context.h>

#include <grpcpp/channel.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/ext/health_check_service_server_builder_option.h>
#include <grpcpp/resource_quota.h>
#include <grpcpp/security/credentials.h>
#include <grpcpp/support/channel_arguments.h>

#if !defined(_win_) && !defined(_cygwin_)
#include <sys/un.h>
#endif

namespace {
    void PrintException(TLog& log, const std::exception& exc) {
        if (/* auto sysErr = */ dynamic_cast<const TSystemError*>(&exc)) {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[SERV]") << "TSystemError: " << exc.what() << ". Shutting down.." << Endl;
        } else {
            LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[SERV]") << "std::exception :" << exc.what() << ". Shutting down.." << Endl;
        }
    }

    template <typename Func>
    bool CatchExceptions(Func&& func, TLog& log) {
        try {
            func();
        } catch (const std::exception& e) {
            PrintException(log, e);
            return false;
        }
        return true;
    }

    const TString DefaultAddress = ToString(NUserServicePrivate::Inet);

    static TMaybe<i64> GetI64Param(const NConfig::TConfig& config, NUserServicePrivate::EConfigStrings e) {
        using namespace NUserServicePrivate;
        using namespace NConfig;

        auto section = !config.IsNull() && config.Get<TDict>().contains(ToString(GrpcServer)) ? config.Get<TDict>().At(ToString(GrpcServer)).Get<TDict>() : TDict();

        return section.contains(ToString(e)) ? MakeMaybe(section.At(ToString(e)).As<i64>()) : TMaybe<i64>();
    }

#if !defined(_win_) && !defined(_cygwin_)
    static TString GetFreeSocketName() {
        try {
            TString name = MakeTempName(nullptr, "psing");
            TFsPath(name).DeleteIfExists();
            return name.size() < sizeof(((sockaddr_un*)nullptr)->sun_path) ? name : TString("");
        } catch (const yexception&) {
            // If TMPDIR is empty then util/folder/dirut.cpp throws non-specific exception
            return "";
        }
    }
#endif

    static TSystemWideName GetAddress(const NConfig::TConfig& config) {
        using namespace NUserServicePrivate;
        using namespace NConfig;

        auto section = !config.IsNull() && config.Get<TDict>().contains(ToString(GrpcServer)) ? config.Get<TDict>().At(ToString(GrpcServer)).Get<TDict>() : TDict();

        auto address = section.contains(ToString(Address)) ? section.At(ToString(Address)).Get<TString>() : DefaultAddress;

#if defined(_win_) || defined(_cygwin_)
        if (address == ToString(Local)) {
            address = DefaultAddress;
        }
#else
        if (address == ToString(Local)) {
            TString socketName = GetFreeSocketName();
            if (socketName != "") {
                TSockAddrLocalStream servAddr(socketName.c_str());
                return TSystemWideName::GetMyName(servAddr);
            } else {
                address = DefaultAddress;
            }
        }
#endif
        if (address == ToString(Inet)) {
            TSockAddrInet servAddr("127.0.0.1", 0);
            return TSystemWideName::GetMyName(servAddr);
        }
        if (address == ToString(Inet6)) {
            TSockAddrInet6 servAddr("::1", 0);
            return TSystemWideName::GetMyName(servAddr);
        }
        Y_UNREACHABLE();
    }
}

namespace {
    class TShutdownGuard {
    public:
        template <typename T>
        static inline void Destroy(T* server) noexcept {
            server->Shutdown();
            if (server->GetExitCode() == 0) {
                server->SetExitCode(NUserService::GrpcInstallServerGuardEC);
            }
        }
    };

    // grpc server deletes its health service, but we do not want to delete TSingletonServer.
    class TDetachedIServer : TNonCopyable, public grpc::HealthCheckServiceInterface {
    public:
        TDetachedIServer(NUserService::TSingletonServer* app)
            : App_(app)
        {
        }
        ~TDetachedIServer() {
            App_ = nullptr;
        }
        void Shutdown() override {
            App_->Shutdown();
        }
        void SetServingStatus(const grpc::string& name, bool serving) override {
            App_->SetServingStatus(name, serving);
        }
        void SetServingStatus(bool serving) override {
            App_->SetServingStatus(serving);
        }

    private:
        NUserService::TSingletonServer* App_;
    };

    /// https://github.com/grpc/grpc/issues/10755
    /// Occasionally got UNIMPLEMENTED for GRPC
    struct TSetNoReusePort: public grpc::ServerBuilderOption {
        void UpdateArguments(grpc::ChannelArguments* args) override {
            args->SetInt(GRPC_ARG_ALLOW_REUSEPORT, 0);
        }
        void UpdatePlugins(std::vector<std::unique_ptr<grpc::ServerBuilderPlugin>>*) override {
        }
    };
}

namespace NUserService {
    using namespace grpc;

    TSingletonServer::TSingletonServer(const NConfig::TConfig& config, TLog& log)
        : Config_(config)
        , Log_(log)
        , QuiescentDelay_(0)
        , State_(static_cast<TAtomic>(NUserService::Suspended))
        , ExitCode_(0)
    {
    }

    TSingletonServer::~TSingletonServer() {
        Shutdown();
        WaitForCompletion();
        Cleanup(TSystemWideName(), [](TSystemWideName) -> void {});
    }

    // TODO: add configuration parameters.
    void TSingletonServer::BuildAndStartService() {
        constexpr int MAX_MESSAGE_LENGTH = 8 * (1 << 20);

        ServerBuilder builder;

        int port = 0;

        MyName_ = GetAddress(Config_);
        builder.AddListeningPort(MyName_.ToGrpcAddress(), InsecureServerCredentials(), &port);
        builder.SetMaxReceiveMessageSize(MAX_MESSAGE_LENGTH);
        builder.SetMaxSendMessageSize(MAX_MESSAGE_LENGTH);

        // Create an instance owned by 'this'.
        ErrorHandler_ = [this](TLog& log, const std::exception& e) -> void {
            PrintException(log, e);
            Shutdown();
            int rc = ClassifyException(e);
            SetExitCode(rc == NoSpcEC || rc == NoMemEC ? rc : CriticalErrorHandlerEC);
        };

        for (auto& s : Services_) {
            builder.RegisterService(s->GetGrpcService(Config_, Log_, ErrorHandler_, this));
        }

        {
            UserManagementService_.Reset(new NUserService::TService(*this, Log_));
            builder.RegisterService(UserManagementService_.Get());
        }
        {
            HealthCheckService_.Reset(new NUserService::THealthCheck(this));
            builder.RegisterService(HealthCheckService_.Get());
        }
        {
            // ResourceQuota quota;
            // quota.SetMaxThreads(4);
            builder.SetSyncServerOption(ServerBuilder::NUM_CQS, 3);
            // builder.SetSyncServerOption(ServerBuilder::MIN_POLLERS, 2);
            // builder.SetSyncServerOption(ServerBuilder::MAX_POLLERS, 2);
            builder.SetSyncServerOption(ServerBuilder::CQ_TIMEOUT_MSEC, 1000);
            // builder.SetResourceQuota(quota);
        }

        // Wait for TString == std::string.
        {
            EnableDefaultHealthCheckService(false);

            std::unique_ptr<HealthCheckServiceInterface> service(new TDetachedIServer(this));

            std::unique_ptr<ServerBuilderOption> option(new HealthCheckServiceServerBuilderOption(std::move(service)));

            builder.SetOption(std::move(option));
            builder.SetOption(std::make_unique<TSetNoReusePort>());
        }

        Server_.Reset(builder.BuildAndStart().release());
        MyName_.SetPort(static_cast<TIpPort>(port));
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Starting server at " << MyName_.ToGrpcAddress() << ", pid=" << MyName_.GetPid() << Endl;
        Y_ENSURE_EX(port > 0 || !MyName_.GetLocalSocket().empty(), TWithBackTrace<yexception>());

        WaitThread_ = std::thread([this]() {
            // TODO: Add CondVar?, but be cautious of locks for Server_ (sighandlers, exception
            // handlers, etc.)
            while (static_cast<NUserService::EState>(AtomicGet(State_)) != NUserService::ShuttingDown) {
                usleep(WAIT_MSEC_SLEEP * 1000);
            }
            // Shutdown BE before GRPC services to avoid conflicts during new server installation
            // (get cleaner diag).
            StopProcessing();
            Server_->Shutdown();
        });
    }

    // This code is executed while lock is held in TType (TPSingleton).
    // No need to use ServerMutex_ here.
    TSystemWideName TSingletonServer::InstallServer(TSystemWideName stale, std::function<void(TSystemWideName)> install) {
        auto start = TInstant::Now();
        // Shutdown if anything goes wrong.
        THolder<TSingletonServer, TShutdownGuard> resetter(this);

        // If it is force replacement then kill peer. No real lock update, dummy 'install'.
        (void)HardStop(stale, [](TSystemWideName) -> void {});

        Y_ENSURE_EX(!stale.CheckProcess(), TWithBackTrace<yexception>());

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Initializing GRPC server..." << Endl;
        BuildAndStartService();

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Started GRPC server." << Endl;
        // Mark myself as THE server.
        install(MyName_);
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Installed GRPC server." << Endl;

        StartProcessing();
        SetServingStatus(true);

        QuiescentDelay_ = 0;
        for (auto& s : Services_) {
            QuiescentDelay_ = Max(QuiescentDelay_, s->GetBackGroundService()->PollingDelay());
        }

        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Started processing" << Endl;

        // Remove guards.
        (void)resetter.Release();
        auto end = TInstant::Now();
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Initialization time: "
                                                                                       << end - start << Endl;
        return MyName_;
    }

    // This code is executed while lock is held in TType (TPSingleton).
    // No need to use ServerMutex_ here.
    TSystemWideName TSingletonServer::HardStop(TSystemWideName stale, std::function<void(TSystemWideName)> install) {
        auto start = TInstant::Now();
        using namespace NUserServicePrivate;
        Y_DEFER {
            install(TSystemWideName());
            auto end = TInstant::Now();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Shutdown time: "
                                                                                           << end - start << Endl;
        };

        bool graceful = false;
        if (stale.CheckProcess()) {
            ChannelArguments args;
            args.SetMaxReceiveMessageSize(128);
            args.SetMaxSendMessageSize(128);
            ResourceQuota quota("memory_bound");
            args.SetResourceQuota(quota.Resize(1024 * 1024).SetMaxThreads(1));

            try {
                // TODO: process resource exhaustion.
                auto deadline = GetI64Param(Config_, ShutdownDealline);
                auto s = NUserService::TClient(CreateCustomChannel(stale.ToGrpcAddress(), InsecureChannelCredentials(), args)).Shutdown("", deadline.Empty() ? 0 : deadline.GetRef());
                graceful = s == NUserService::ShuttingDown;
            } catch (const TGrpcException& exc) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ERR, "ERR[SERV]") << "Will terminate server using term/kill sequence" << Endl;
            }
        } else if (!stale.TProcessUID::CheckProcess()) {
            return TSystemWideName();
        }

        if (!graceful) {
#if defined(_unix_)
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ERR, "ERR[SERV]") << "Sending TERM signal" << Endl;
            NUserService::SendSignal(stale, SIGCONT);
            NUserService::SendSignal(stale, SIGTERM);
#endif
        } else {
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Gracefully shut down server at " << stale.ToPlainString() << Endl;
        }

        auto waitTime = GetI64Param(Config_, TermSignalWait);
        // Wait process to exit
        for (i64 i = 0, e = waitTime.Empty() ? 60 : waitTime.GetRef(); i != e; ++i) {
            sleep(1);
            if (!stale.TProcessUID::CheckProcess()) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Stopped in " << i + 1 << "s" << Endl;
                return TSystemWideName();
            }
        }

#if defined(_unix_)
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_CRIT, "CRIT[SERV]") << "Sending KILL signal" << Endl;
        NUserService::SendSignal(stale, SIGKILL);
#endif
        return TSystemWideName();
    }

    void TSingletonServer::Shutdown() {
        // Should not grab ServerMutex_ without recursive locks.
        AtomicSet(State_, static_cast<TAtomic>(NUserService::ShuttingDown));
    }

    void TSingletonServer::StartProcessing() noexcept {
        AtomicSet(LastAccessTime_, MilliSeconds());
        if (AtomicGet(State_) != static_cast<TAtomic>(NUserService::Suspended)) {
            return;
        }

        TWriteGuard lock(ServerMutex_);
        if (!AtomicCas(&State_, static_cast<TAtomic>(NUserService::Processing), static_cast<TAtomic>(NUserService::Suspended))) {
            return;
        }

        if (!Server_.Get()) {
            return;
        }

        auto startAll = [this]() -> void {
            for (auto& s : Services_) {
                s->GetBackGroundService()->StartProcessing();
            }
        };

        if (!CatchExceptions(std::move(startAll), Log_)) {
            lock.Release();
            Shutdown();
            SetExitCode(StartStopErrorEC);
        }
    }

    void TSingletonServer::StopProcessing() noexcept {
        AtomicSet(LastAccessTime_, MilliSeconds());
        if (AtomicGet(State_) != static_cast<TAtomic>(NUserService::Processing)) {
            return;
        }

        TWriteGuard lock(ServerMutex_);
        if (!AtomicCas(&State_, static_cast<TAtomic>(NUserService::Suspended), static_cast<TAtomic>(NUserService::Processing))) {
            return;
        }
        if (!Server_.Get()) {
            return;
        }

        auto stopAll = [this]() -> void {
            for (auto& s : Services_) {
                s->GetBackGroundService()->StopProcessing();
            }
        };

        if (!CatchExceptions(std::move(stopAll), Log_)) {
            lock.Release();
            Shutdown();
            SetExitCode(StartStopErrorEC);
        }
    }

    void TSingletonServer::SetServingStatus(const grpc::string& service, bool serving) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "SetServingStatus: service=" << service << Endl;
        this->SetServingStatus(serving);
    }

    void TSingletonServer::SetServingStatus(bool serving) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "SetServingStatus: serving=" << serving << Endl;
        if (serving) {
            StartProcessing();
        } else {
            StopProcessing();
        }
    }

    NUserService::EState TSingletonServer::GetState() const noexcept {
        return static_cast<NUserService::EState>(AtomicGet(State_));
    }

    void TSingletonServer::SetMasterMode(bool master) {
        AtomicSet(LastAccessTime_, MilliSeconds());
        if (AtomicGet(State_) == static_cast<TAtomic>(NUserService::ShuttingDown)) {
            return;
        }

        TWriteGuard lock(ServerMutex_);
        for (auto& s : Services_) {
            s->GetBackGroundService()->SetMasterMode(master);
        }
    }

    bool TSingletonServer::GetMasterMode() const noexcept {
        if (AtomicGet(State_) == static_cast<TAtomic>(NUserService::ShuttingDown)) {
            return false;
        }

        TReadGuard lock(ServerMutex_);
        for (auto& s : Services_) {
            if (s->GetBackGroundService()->GetMasterMode()) {
                return true;
            }
        }
        return false;
    }

    bool TSingletonServer::IsQuiescent() const noexcept {
        if (AtomicGet(State_) != static_cast<TAtomic>(NUserService::Processing)) {
            return false;
        }

        TReadGuard lock(ServerMutex_);
        for (auto& s : Services_) {
            if (!s->GetBackGroundService()->IsQuiescent()) {
                return false;
            }
        }
        ui64 now = MilliSeconds();
        ui64 last = static_cast<ui64>(AtomicGet(LastAccessTime_));
        return now >= static_cast<ui64>(QuiescentDelay_) + last;
    }

    void TSingletonServer::WaitForCompletion() noexcept {
        if (Server_.Get()) {
            Server_->Wait();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Server stopped" << Endl;
        }
    }

    TSystemWideName TSingletonServer::Cleanup(TSystemWideName stale, std::function<void(TSystemWideName)> install) noexcept {
        auto finalizeProcessing = [&install, &stale, this]() -> TSystemWideName {
            auto newproc = stale == MyName_ ? TSystemWideName() : stale;
            install(newproc);
            return newproc;
        };

        if (WaitThread_.joinable()) {
            WaitThread_.join();
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Polling thread stopped" << Endl;
        }

        TWriteGuard lock(ServerMutex_);
        if (Server_.Get() == nullptr) {
            return finalizeProcessing();
        }
        Server_.Reset(nullptr);
        UserManagementService_.Reset(nullptr);
        HealthCheckService_.Reset(nullptr);
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Aux services cleared" << Endl;
        for (auto& s : Services_) {
            s.Reset(nullptr);
        }
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Back-end services cleared" << Endl;

        auto socketName = MyName_.GetLocalSocket();
        if (!socketName.empty()) {
            TFsPath(socketName).DeleteIfExists();
        }
        return finalizeProcessing();
    }

    TAtomic TSingletonServer::LastAccessTime_;
}

namespace NUserService {
    void TShutdownPoller::Initialize() {
        if (!WorkThread_.joinable()) {
            PollingDelay_ = Max(Server_.PollingDelay(), 1000); // At lease 1 second
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Check for no activity every " << PollingDelay_ << "ms" << Endl;
            WorkThread_ = std::thread([this]() { this->WorkLoop(); });
        }
    }

    void TShutdownPoller::Finalize() {
        if (WorkThread_.joinable()) {
            AtomicSet(Shutdown_, 1);
            WorkThread_.join();
            WorkThread_ = std::thread();
        }
    }

    void TShutdownPoller::WorkLoop() {
        do {
            for (int i = 0; i != PollingDelay_ / 1000; ++i) {
                if (ReturnImmediately((i % 10) == 9)) {
                    Server_.Shutdown();
                    return;
                }
                sleep(1);
            }

            if (Server_.IsQuiescent()) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "No activity seen. Shutting down gracefully... " << Endl;
                Server_.Shutdown();
                return;
            }
        } while (true);
    }

    bool TShutdownPoller::ReturnImmediately(bool majorCheck) {
        if (AtomicGet(Shutdown_)) {
            return true;
        }

        if (majorCheck && !LockName_.Exists()) {
            LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ALERT, "ALERT[SERV]") << "Lock file: " << ToString(LockName_) << " was deleted" << Endl;
            Server_.SetExitCode(NUserService::ExternalErrorEC);
            Server_.Shutdown();
            return true;
        }
        return false;
    }

    int ClassifyException(const std::exception& ex) {
        using namespace NSQLite;
        if (auto* e = dynamic_cast<const TSQLiteError*>(&ex)) {
            if (e->GetErrorCode() == SQLITE_FULL) {
                return NoSpcEC;
            } else if (e->GetErrorCode() == SQLITE_NOMEM) {
                return NoMemEC;
            } else if (EqualToOneOf(e->GetErrorCode(), SQLITE_IOERR, SQLITE_CORRUPT, SQLITE_PERM, SQLITE_READONLY)) {
                return IOEC;
            }
            return GenericExceptionEC;
        } else if (auto* e = dynamic_cast<const TSystemError*>(&ex)) {
            if (e->Status() == ENOSPC) {
                return NoSpcEC;
            } else if (e->Status() == ENOMEM) {
                return NoMemEC;
            }
            if (/* auto* e = */ dynamic_cast<const TIoException*>(&ex)) {
                return IOEC;
            }
            return GenericExceptionEC;
        } else if (/* auto* e = */ dynamic_cast<const TGrpcException*>(&ex)) {
            return GrpcClientExceptionEC;
        } else if (/* auto* e = */ dynamic_cast<const yexception*>(&ex)) {
            return GenericExceptionEC;
        } else if (/* auto* e = */ dynamic_cast<const std::bad_alloc*>(&ex)) {
            return NoMemEC;
        } else {
            return GenericExceptionEC;
        }
    }
}
