#pragma once

#include "devtools/local_cache/psingleton/service/service.h"
#include "devtools/local_cache/psingleton/systemptr.h"

#include <library/cpp/config/config.h>
#include <library/cpp/deprecated/atomic/atomic.h>

#include <util/generic/list.h>
#include <util/system/rwlock.h>

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

#include <thread>

namespace NUserServicePrivate {
    /// Simple wrapper to provide common interface to IService and grpc::Service, which is a base of real auto-generated service.
    ///
    /// Avoid complications with multiple inheritance. Need to initialize instance on demand in the TSingletonServer class below.
    /// \see template<typename> ServiceInitializer below.
    template <typename CriticalErrorHandler>
    class IServiceWithBackend {
    public:
        virtual ~IServiceWithBackend() {
        }
        virtual NUserService::IService* GetBackGroundService() = 0;
        virtual grpc::Service* GetGrpcService(NConfig::TConfig& config, TLog& log, const CriticalErrorHandler& handler, const NUserService::IServer* server) = 0;
        virtual const char* GetName() = 0;
    };

    /// \see IServiceWithBackend
    /// Only ptr + vtable.
    template <typename BackGroundService, typename CriticalErrorHandler, const char* Name>
    class ServiceInitializer final : TNonCopyable, public IServiceWithBackend<CriticalErrorHandler> {
        static_assert(std::is_base_of<NUserService::IService, BackGroundService>::value, "BackGroundService should implement IBackgroudService");
        static_assert(std::is_base_of<grpc::Service, BackGroundService>::value, "BackGroundService should be grpc::Service");

    public:
        grpc::Service* GetGrpcService(NConfig::TConfig& config, TLog& log, const CriticalErrorHandler& handler, const NUserService::IServer* server) override {
            if (!Ref_.Get()) {
                Ref_.Reset(new BackGroundService(config, log, handler, server));
            }
            return Ref_.Get();
        }
        NUserService::IService* GetBackGroundService() override {
            return Ref_.Get();
        }
        const char* GetName() override {
            return Name;
        }

    private:
        THolder<BackGroundService> Ref_;
    };
}

namespace NUserService {
    using TCriticalErrorHandler = std::function<void(TLog&, const std::exception&)>;

    class TSingletonServer : TNonCopyable, public NUserService::IServer {
    public:
        TSingletonServer(const NConfig::TConfig& config, TLog& log);
        ~TSingletonServer();

        // NUserService::IServer @{
        void Shutdown() override;
        void SetServingStatus(const grpc::string&, bool) override;
        void SetServingStatus(bool serving) override;
        NUserService::EState GetState() const noexcept override;

        int GetExitCode() const noexcept override {
            return static_cast<int>(AtomicGet(ExitCode_));
        }

        void SetMasterMode(bool master) override;
        bool GetMasterMode() const noexcept override;
        // @}

        // TPSingleton interface @{
        // Terminate previous server, install new one.
        TSystemWideName InstallServer(TSystemWideName stale, std::function<void(TSystemWideName)> install);
        // Simply terminate previous server.
        TSystemWideName HardStop(TSystemWideName stale, std::function<void(TSystemWideName)> install);
        TSystemWideName Cleanup(TSystemWideName stale, std::function<void(TSystemWideName)> install) noexcept;
        // @}

        void WaitForCompletion() noexcept;

        template <typename BackGroundService, const char* Name>
        void AddService() {
            Services_.emplace_back(new NUserServicePrivate::ServiceInitializer<BackGroundService, TCriticalErrorHandler, Name>());
        }

        /// Sets non-zero exit code once.
        void SetExitCode(int code) noexcept {
            AtomicCas(&ExitCode_, static_cast<TAtomic>(code), static_cast<TAtomic>(0));
        }

        bool IsQuiescent() const noexcept;

        /// Delay to poll for quiescence.
        int PollingDelay() const noexcept {
            return QuiescentDelay_;
        }

        constexpr static int WAIT_MSEC_SLEEP = 50;

    private:
        void BuildAndStartService();
        void StopProcessing() noexcept;
        void StartProcessing() noexcept;

    private:
        /// Pure GRPC functionality @{
        THolder<grpc::Server> Server_;
        /// Polling for Shutdown signal.
        std::thread WaitThread_;
        /// @}

        TCriticalErrorHandler ErrorHandler_;

        /// Management service.
        THolder<NUserService::TService> UserManagementService_;
        /// Specific health service.
        THolder<NUserService::THealthCheck> HealthCheckService_;

        /// Other services.
        TList<THolder<NUserServicePrivate::IServiceWithBackend<TCriticalErrorHandler>>> Services_;

        /// Guard access to Server_ from NUserService::IServer's methods (called from health service).
        TRWMutex ServerMutex_;

        /// Parameters for IServiceWithBackend @{
        NConfig::TConfig Config_;
        TLog& Log_;
        /// @}

        TSystemWideName MyName_;

        /// Max of all PollingDelay in Services_.
        int QuiescentDelay_;
        /// NUserService::EState type in practice.
        TAtomic State_;
        /// Exit code
        TAtomic ExitCode_;

        // Quiescence support of the server itself.
        static TAtomic LastAccessTime_;
    };

    enum EServerExitCode {
        GenericExceptionEC = -1,
        InstallServerGuardEC = -2,
        CriticalErrorHandlerEC = -3,
        StartStopErrorEC = -4,
        ExternalErrorEC = -5,
        TermErrorEC = -6,
        PreconditionEC = -7,
        NoMemEC = -8,
        NoSpcEC = -9,
        IOEC = -10,
        GrpcInstallServerGuardEC = -11,
        GrpcClientExceptionEC = -12,
    };

    int ClassifyException(const std::exception& e);

    /// Checks if 'rm -rf ~/.ya' was executed.
    /// Checks if backend are quiescent for some time.
    class TShutdownPoller : TNonCopyable {
    public:
        TShutdownPoller(TSingletonServer& server, TLog& log, const TString& lockName)
            : LockName_(lockName)
            , Server_(server)
            , Log_(log)
            , Shutdown_(0)
            , PollingDelay_(0)
        {
        }

        ~TShutdownPoller() {
            Finalize();
        }

        void Initialize();

        void Finalize();

    private:
        void WorkLoop();

        bool ReturnImmediately(bool majorCheck);

        /// Lock file to poll.
        TFsPath LockName_;
        std::thread WorkThread_;
        /// Server to manage.
        TSingletonServer& Server_;
        TLog& Log_;
        /// Termination flag.
        TAtomic Shutdown_;
        /// Sleep delay.
        int PollingDelay_;
    };
}
