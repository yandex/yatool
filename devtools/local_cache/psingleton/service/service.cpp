#include "service.h"

#include "devtools/local_cache/psingleton/systemptr.h"

#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

#include <util/generic/vector.h>
#include <util/random/random.h>
#include <util/string/cast.h>

namespace NUserService {
    using namespace grpc;

    Status TService::SetStatus(TStringBuf method, ServerContext* context) {
        if (context->IsCancelled()) {
            return Status(StatusCode::CANCELLED, ToString(method) + ": deadline exceeded or client cancelled.");
        }
        return Status::OK;
    }

    static void SetServerStatus(EState state, TStatus* response, IServer& server) noexcept {
        // State should correspond to log messages, do not re-query it.
        response->SetState(state);
        response->SetExitCode(server.GetExitCode());
        response->SetMasterMode(server.GetMasterMode());
    }

    Status TService::Shutdown(ServerContext* context, const TShutdown* request, TStatus* response) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[NUS]") << "Shutdown from " << request->GetPeer() << Endl;
        auto state = Server_.GetState();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (state != ShuttingDown) {
                Server_.Shutdown();
                state = Server_.GetState();
            }
        });
        Y_ABORT_UNLESS(state == ShuttingDown);
        SetServerStatus(ShuttingDown, response, Server_);
        return SetStatus("Shutdown", context);
    }

    Status TService::StopProcessing(ServerContext* context, const TStopProcessing* request, TStatus* response) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[NUS]") << "StopProcessing from " << request->GetPeer() << Endl;
        auto state = Server_.GetState();
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (state == Processing) {
                Server_.SetServingStatus(false);
                state = Server_.GetState();
            }
            if (state != Suspended) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ERR,
                                           "ERR[NUS]")
                    << "Race in StopProcessing, req from " << request->GetPeer()
                    << ", state=" << (int)state << Endl;
            }
        });
        SetServerStatus(state, response, Server_);
        return SetStatus("StopProcessing", context);
    }

    Status TService::StartProcessing(ServerContext* context, const TStartProcessing* request, TStatus* response) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[NUS]") << "StartProcessing from " << request->GetPeer() << Endl;
        auto state = Server_.GetState();
        if (state == ShuttingDown) {
            return Status(StatusCode::FAILED_PRECONDITION, "StartProcessing: server is shutting down.");
        }
        REPORT_EXCEPTION_IDEMPOTENT_METHOD({
            if (state != Processing) {
                Server_.SetServingStatus(true);
                state = Server_.GetState();
            }
            if (state != Processing) {
                LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_ERR,
                                           "ERR[NUS]")
                    << "Race in StartProcessing, req from " << request->GetPeer()
                    << ", state=" << (int)state << Endl;
            }
        });
        SetServerStatus(state, response, Server_);
        return SetStatus("StartProcessing", context);
    }

    Status TService::GetStatus(ServerContext* context, const TPeer*, TStatus* response) {
        // Do not log request.
        auto state = Server_.GetState();
        if (state == ShuttingDown) {
            return Status(StatusCode::FAILED_PRECONDITION, ": server is already shutting down.");
        }
        SetServerStatus(state, response, Server_);
        return SetStatus("GetStatus", context);
    }

    Status TService::SetMasterMode(ServerContext* context, const TMasterMode* request, TStatus* response) {
        LOGGER_CHECKED_GENERIC_LOG(Log_, TRTYLogPreprocessor, TLOG_INFO, "INFO[NUS]") << "SetMasterMode from " << request->GetPeer() << Endl;
        auto state = Server_.GetState();
        if (state == ShuttingDown) {
            return Status(StatusCode::FAILED_PRECONDITION, ": server is already shutting down.");
        }
        REPORT_EXCEPTION_IDEMPOTENT_METHOD(
            { Server_.SetMasterMode(request->GetMasterMode()); });
        SetServerStatus(state, response, Server_);
        return SetStatus("SetMasterMode", context);
    }
}

namespace NUserService {
    void THealthCheck::SetHealthResponse(grpc::health::v1::HealthCheckResponse* response) {
        auto state = Controller_->GetState();
        response->set_status(state != ShuttingDown ? grpc::health::v1::HealthCheckResponse::SERVING : grpc::health::v1::HealthCheckResponse::NOT_SERVING);
    }

    grpc::Status THealthCheck::Check(grpc::ServerContext*, const grpc::health::v1::HealthCheckRequest*, grpc::health::v1::HealthCheckResponse* response) {
        SetHealthResponse(response);
        return Status::OK;
    }

    grpc::Status THealthCheck::Watch(grpc::ServerContext* context, const grpc::health::v1::HealthCheckRequest*, grpc::ServerWriter<grpc::health::v1::HealthCheckResponse>* writer) {
        auto lastState = grpc::health::v1::HealthCheckResponse::UNKNOWN;
        while (!context->IsCancelled()) {
            grpc::health::v1::HealthCheckResponse response;
            SetHealthResponse(&response);
            if (response.status() != lastState) {
                writer->Write(response, ::grpc::WriteOptions());
            }
            lastState = response.status();
            gpr_sleep_until(gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                                         gpr_time_from_millis(1000, GPR_TIMESPAN)));
            if (response.Getstatus() == grpc::health::v1::HealthCheckResponse::NOT_SERVING) {
                return Status(StatusCode::CANCELLED, "Server is shutting down.");
            }
        }
        return Status::OK;
    }
}

namespace {
    using namespace grpc;
    ClientContext& SetDeadline(ClientContext& ctx, int deadline) {
        if (deadline != INT_MAX && deadline != 0) {
            ctx.set_wait_for_ready(true);
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(deadline));
        }
        return ctx;
    }
}

namespace NUserService {
    EState TClient::Shutdown(const TString& taskId, int deadline) {
        TShutdown req;
        GetMyName(taskId, req.MutablePeer());

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->Shutdown(&SetDeadline(ctx, deadline), req, &state);
        if (r.ok()) {
            return state.GetState();
        }
        ythrow TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    EState TClient::StopProcessing(const TString& taskId, int deadline) {
        TStopProcessing req;
        GetMyName(taskId, req.MutablePeer());

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->StopProcessing(&SetDeadline(ctx, deadline), req, &state);
        if (r.ok()) {
            return state.GetState();
        }
        ythrow TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    EState TClient::StartProcessing(const TString& taskId, int deadline) {
        TStartProcessing req;
        GetMyName(taskId, req.MutablePeer());

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->StartProcessing(&SetDeadline(ctx, deadline), req, &state);
        if (r.ok()) {
            return state.GetState();
        }
        ythrow TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    EState TClient::SetMasterMode(const TString& taskId, bool master, int deadline) {
        TMasterMode req;
        GetMyName(taskId, req.MutablePeer());
        req.SetMasterMode(master);

        TStatus state;
        ClientContext ctx;
        auto r = Stub_->SetMasterMode(&SetDeadline(ctx, deadline), req, &state);
        if (r.ok()) {
            return state.GetState();
        }
        ythrow TGrpcException(r.error_message(), r.error_code()) << ", " << r.error_message();
    }

    TPeer* GetMyName(const TString& taskId, TPeer* out, ETaskNameMode mode) {
        auto uid = TProcessUID::GetMyName();
        out->MutableProc()->SetPid(uid.GetPid());
        out->MutableProc()->SetStartTime(uid.GetStartTime());
        switch (mode) {
            case ExactName:
                out->SetTaskGSID(taskId);
                break;
            case RandomizeName:
                out->SetTaskGSID(taskId + "-" + ToString(RandomNumber<ui64>()));
                break;
        }
        return out;
    }
}
