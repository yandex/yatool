#include "server.h"

#include <devtools/executor/proc_util/proc_util.h>
#include <devtools/executor/proto/runner.grpc.pb.h>

#include <grpc/grpc.h>

#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>

#include <library/cpp/deprecated/atomic/atomic.h>
#include <library/cpp/sighandler/async_signals_handler.h>

#include <util/generic/list.h>
#include <util/stream/file.h>
#include <util/string/builder.h>
#include <util/system/atexit.h>
#include <util/system/condvar.h>
#include <util/system/error.h>
#include <util/system/shellcommand.h>
#include <util/thread/factory.h>


namespace {
    TAtomic ShutdownRequested(0);

    void RequestShutdown(int) {
        AtomicAdd(ShutdownRequested, 1);
    }

    bool IsShutdownRequested() {
        return AtomicGet(ShutdownRequested);
    }

    struct TSyncData {
        TSyncData()
            : Signaled(false)
        {
        }

        TMutex Lock;
        TCondVar CondVar;
        bool Signaled;
    };

    class TSaturatingStream: public IOutputStream {
    public:
        TSaturatingStream(grpc::ServerWriter<NExternalExecutor::TResponse>* writer, TSyncData* syncData, bool cacheData)
            : Writer(writer)
            , SyncData(syncData)
            , Synced(false)
        {
            if (cacheData) {
                WriteOptions.set_buffer_hint();
            }
        }

    protected:
        void DoWrite(const void* buf, size_t len) override {
            Y_ASSERT(buf);
            Sync();
            NExternalExecutor::TResponse response;
            response.AddStderrLines((const char*)buf, len);
            Writer->Write(response, WriteOptions);
        }

        void DoWriteV(const TPart* parts, size_t count) override {
            Y_ASSERT(parts);
            Sync();
            NExternalExecutor::TResponse response;
            for (size_t i = 0; i < count; i++) {
                response.AddStderrLines((const char*)parts->buf, parts->len);
            }
            Writer->Write(response, WriteOptions);
        };

    private:
        void Sync() {
            if (!Synced) {
                with_lock (SyncData->Lock) {
                    SyncData->CondVar.Wait(SyncData->Lock, [&] { return SyncData->Signaled; });
                    Synced = true;
                }
            }
        };

    private:
        grpc::ServerWriter<NExternalExecutor::TResponse>* Writer;
        TSyncData* SyncData;
        bool Synced;
        grpc::WriteOptions WriteOptions;
    };

    TShellCommandOptions DefaultCommandOptions() {
        TShellCommandOptions options;
        options.SetAsync(true);
        options.SetClearSignalMask(true);
        options.SetCloseAllFdsOnExec(false);
        options.SetCloseStreams(true);
        // We don't want to lose controlling terminal. Otherwise --gdb and --pdb won't work
        options.SetDetachSession(false);
        options.SetLatency(20);
        options.SetUseShell(false);
        // SetUseShell disables quoting if it's enabled. Call SetQuoteArguments after SetUseShell.
        // It's safe to set quote on the windows, because args will be quoted only if it's required.
        options.SetQuoteArguments(true);
        return options;
    }
}

class RunnerImpl final: public NExternalExecutor::Runner::Service {
public:
    RunnerImpl(bool cacheStderr)
        : CacheStderr(cacheStderr)
    {
    }

    grpc::Status Ping(grpc::ServerContext* /*context*/,
                      const NExternalExecutor::TEmpty* /*request*/,
                      NExternalExecutor::TEmpty* /*response*/) override {
        return grpc::Status::OK;
    }

    grpc::Status Execute(grpc::ServerContext* context,
                         const NExternalExecutor::TCommand* command,
                         grpc::ServerWriter<NExternalExecutor::TResponse>* writer) override {
        Y_UNUSED(context);
        Y_ASSERT(command);
        Y_ASSERT(writer);

        try {
            return ProcessExecute(command, writer);
        } catch (...) {
            NExternalExecutor::TResponse response;
            response.SetError(TStringBuilder{} << "Internal executor's error: " << CurrentExceptionMessage());
            writer->Write(response);
            return grpc::Status::OK;
        }
    }

    grpc::Status ProcessExecute(const NExternalExecutor::TCommand* command,
                                grpc::ServerWriter<NExternalExecutor::TResponse>* writer) {
        NExternalExecutor::TResponse response;

        if (IsShutdownRequested()) {
            response.SetError("Shutdown in progress");
            writer->Write(response);
            return grpc::Status::OK;
        }

        if (command->args_size() == 0) {
            response.SetPid(0);
            response.SetError("Empty command");
            writer->Write(response);
            return grpc::Status::OK;
        }

        TList<TString> args;
        for (int i = 1; i < command->args_size(); i++) {
            args.push_back(command->GetArgs(i));
        }

        THolder<IOutputStream> stdoutHolder;
        if (command->GetStdoutFilename()) {
            try {
                stdoutHolder.Reset(new TFileOutput(TFile(command->GetStdoutFilename(), CreateAlways | WrOnly | Seq | CloseOnExec)));
            } catch (yexception& exc) {
                response.SetError(exc.what());
                writer->Write(response);
                return grpc::Status::OK;
            }
        }

        TSyncData syncData;

        THolder<IOutputStream> stderrHolder;
        if (command->GetStderrFilename()) {
            try {
                stderrHolder.Reset(new TFileOutput(TFile(command->GetStderrFilename(), CreateAlways | WrOnly | Seq | CloseOnExec)));
            } catch (yexception& exc) {
                response.SetError(exc.what());
                writer->Write(response);
                return grpc::Status::OK;
            }
        } else {
            stderrHolder.Reset(new TSaturatingStream(writer, &syncData, CacheStderr));
        }

        TShellCommandOptions options = DefaultCommandOptions();
        options.SetErrorStream(stderrHolder.Get());
        options.SetOutputStream(stdoutHolder.Get());
        options.SetNice(command->GetNice());
#if defined(_linux_)
        const NExternalExecutor::TRequirements requirements = command->GetRequirements();
        if (requirements.GetNetwork() == NExternalExecutor::TRequirements::RESTRICTED) {
            options.SetFuncAfterFork(NProcUtil::UnshareNs);
        }
#endif
        for (const auto& env : command->GetEnv()) {
            options.Environment[env.GetName()] = env.GetValue();
        }
        options.Environment["_BINARY_EXEC_TIMESTAMP"] = ToString(TInstant::Now().MicroSeconds());
#if defined(_win_)
        // Problem is still rarely reproducing
        // https://wpdev.uservoice.com/forums/110705-universal-windows-platform/suggestions/18355615-ucrtbase-bug-wspawnve-is-broken
        options.Environment["=C:"] = "C:\\";
#endif

        TShellCommand cmd(command->GetArgs(0), args, options, command->GetCwd());
        try {
            cmd.Run();
        } catch (yexception& exc) {
            // Run() will throw exception on the windows if the length of the command exceeds 32Kib
            response.SetError(TStringBuilder{} << "Process wasn't launched: " << exc.what());
            writer->Write(response);
            return grpc::Status::OK;
        }

        if (cmd.GetPid() <= 0) {
            // TOFIX windows encoding problems
            // response.SetError(cmd.GetError());
            response.SetError("Process wasn't launched");
            writer->Write(response);
            return grpc::Status::OK;
        }

        // It's guaranteed that the first response will contain pid
        response.SetPid(cmd.GetPid());
        writer->Write(response);

        with_lock (syncData.Lock) {
            syncData.Signaled = true;
        };
        syncData.CondVar.Signal();

        cmd.Wait();

        // Streams are already flushed, ExitCode will be the last response
        int rc = *cmd.GetExitCode();
        if (rc == 0 && cmd.GetStatus() == TShellCommand::ECommandStatus::SHELL_ERROR) {
            response.SetExitCode(1);
        } else if (!cmd.GetExitCode().Defined()) {
            response.SetExitCode(1);
        } else {
            response.SetExitCode(rc);
        }
        writer->Write(response);

        return grpc::Status::OK;
    }

private:
    bool CacheStderr;
};

void RunServer(const TString address, bool cacheStderr, bool debug) {
    NProcUtil::TSubreaperApplicant applicant = NProcUtil::TSubreaperApplicant();

    SetAsyncSignalHandler(SIGINT, RequestShutdown);
    SetAsyncSignalHandler(SIGTERM, RequestShutdown);

    RunnerImpl service(cacheStderr);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address.c_str(), grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server(builder.BuildAndStart());
    if (debug) {
        Cerr << "Server has started at: " << address << Endl;
    }
    SystemThreadFactory()->Run([&]() {
        while (!IsShutdownRequested()) {
            usleep(10 * 1000);
        }

        NProcUtil::TerminateChildren();
        server->Shutdown();
    });

    server->Wait();
    applicant.Close();
}
