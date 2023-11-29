#pragma once

#include "devtools/local_cache/psingleton/proto/known_service.grpc.pb.h"

#include <contrib/libs/grpc/src/proto/grpc/health/v1/health.grpc.pb.h>

#include <library/cpp/logger/log.h>

#include <util/generic/fwd.h>
#include <util/generic/yexception.h>

#include <chrono>
#include <grpc/grpc.h>
#include <grpcpp/health_check_service_interface.h>

namespace NUserService {
    class TClient : TNonCopyable {
    public:
        TClient(std::shared_ptr<grpc::ChannelInterface> channel)
            : Stub_(TUserService::NewStub(channel).release())
        {
        }

        EState Shutdown(const TString& taskId, int deadline);
        EState StopProcessing(const TString& taskId, int deadline);
        EState StartProcessing(const TString& taskId, int deadline);
        EState SetMasterMode(const TString& taskId, bool master, int deadline);

    private:
        THolder<TUserService::Stub> Stub_;
    };

    // Reduced functionality so far:
    // no names of services are handled.
    class IServer: public grpc::HealthCheckServiceInterface {
    public:
        virtual EState GetState() const noexcept = 0;
        virtual int GetExitCode() const noexcept = 0;
        /// Set destructive/non-destructive mode.
        /// TODO: selective setting.
        virtual void SetMasterMode(bool master) = 0;
        /// Get destructive/non-destructive mode.
        virtual bool GetMasterMode() const noexcept = 0;
    };

    /// Service managed by IServer.
    class IService {
    public:
        virtual ~IService() {
        }
        /// Stop back-end
        virtual void StopProcessing() = 0;
        /// Start back-end
        virtual void StartProcessing() = 0;
        /// Checks if backend is quiescent and can be stopped;
        virtual bool IsQuiescent() const noexcept = 0;
        /// Delay to poll for quiescence in seconds.
        virtual int PollingDelay() const noexcept = 0;
        /// Set destructive/non-destructive mode.
        virtual void SetMasterMode(bool master) = 0;
        /// Get destructive/non-destructive mode.
        virtual bool GetMasterMode() const noexcept = 0;
    };

    /// External driver of IServer.
    class TService final : TNonCopyable, public TUserService::Service {
    public:
        TService(IServer& serv, TLog& log)
            : Server_(serv)
            , Log_(log)
        {
        }

        // TUserService::Service @{
        grpc::Status Shutdown(grpc::ServerContext* context, const TShutdown* request, TStatus* response) override;
        grpc::Status StopProcessing(grpc::ServerContext* context, const TStopProcessing* request, TStatus* response) override;
        grpc::Status StartProcessing(grpc::ServerContext* context, const TStartProcessing* request, TStatus* response) override;
        grpc::Status SetMasterMode(grpc::ServerContext* context, const TMasterMode* request, TStatus* response) override;
        grpc::Status GetStatus(grpc::ServerContext* context, const TPeer* request, TStatus* response) override;
        // @}

    private:
        grpc::Status SetStatus(TStringBuf method, grpc::ServerContext* context);

        /// External driver of service, also calls Shutdown for GRPC server.
        /// TService is expected to be one of the services handled by Server_.
        IServer& Server_;
        TLog& Log_;
    };

    class THealthCheck final : TNonCopyable, public grpc::health::v1::Health::Service {
    public:
        THealthCheck(const IServer* controller)
            : Controller_(controller)
        {
        }
        // TUserService::Service @{
        grpc::Status Check(grpc::ServerContext*, const grpc::health::v1::HealthCheckRequest*, grpc::health::v1::HealthCheckResponse* res) override;
        grpc::Status Watch(grpc::ServerContext*, const grpc::health::v1::HealthCheckRequest*, grpc::ServerWriter<grpc::health::v1::HealthCheckResponse>*) override;
        // @}

    private:
        void SetHealthResponse(grpc::health::v1::HealthCheckResponse* response);

        const IServer* Controller_;
    };

    /// Exception to catch from failed RPCs in client.
    class TGrpcException: public yexception {
    public:
        TGrpcException(const grpc::string& message, grpc::StatusCode c)
            : Message_(message)
            , Code_(c)
        {
        }

        const grpc::string& GetMessage() const noexcept {
            return Message_;
        }

        grpc::StatusCode GetCode() const noexcept {
            return Code_;
        }

    private:
        grpc::string Message_;
        grpc::StatusCode Code_;
    };

    enum ETaskNameMode {
        ExactName,
        RandomizeName
    };

    TPeer* GetMyName(const TString& taskId, TPeer* out, ETaskNameMode name = ExactName);
}

#define REPORT_EXCEPTION_IDEMPOTENT_METHOD(block)                             \
    try {                                                                     \
        block                                                                 \
    } catch (const yexception& ex) {                                          \
        return Status(StatusCode::UNAVAILABLE,                                \
                      ToString("Unexpected exception caught: ") + ex.what()); \
    } catch (...) {                                                           \
        return Status(StatusCode::UNAVAILABLE,                                \
                      "Unexpected unknown exception caught");                 \
    }
