#include "server-nix.h"

#if defined(_unix_)
#include <library/cpp/logger/global/common.h>
#include <library/cpp/logger/global/rty_formater.h>

namespace NUserService {
    void TTermHandlers::SetHandlers(NUserService::TSingletonServer* server, TLog& log) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sa.sa_flags = SA_SIGINFO | SA_RESTART;
        SigAddSet(&sa.sa_mask, SIGTERM);
        SigAddSet(&sa.sa_mask, SIGINT);
        SigAddSet(&sa.sa_mask, SIGHUP);

        sa.sa_handler = TTermHandlers::SigHandler;

        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Setting signal handlers..." << Endl;
        {
            TSuspendSignals suspend(false /* do NOT ignore errors */);
            AtomicSet(Server_, server);

            Y_ENSURE_EX(sigaction(SIGTERM, &sa, &OldiTermSa_) == 0, TWithBackTrace<yexception>());
            Y_ENSURE_EX(sigaction(SIGINT, &sa, &OldiTermSa_) == 0, TWithBackTrace<yexception>());
            Y_ENSURE_EX(sigaction(SIGHUP, &sa, &OldiHupSa_) == 0, TWithBackTrace<yexception>());
        }
        LOGGER_CHECKED_GENERIC_LOG(log, TRTYLogPreprocessor, TLOG_INFO, "INFO[SERV]") << "Done setting signal handlers" << Endl;
    }

    void TTermHandlers::ResetHandlers() {
        TSuspendSignals suspend(true /* ignore errors */);
        AtomicSet(Server_, nullptr);
        sigaction(SIGHUP, &OldiHupSa_, nullptr);
        sigaction(SIGINT, &OldiTermSa_, nullptr);
        sigaction(SIGTERM, &OldiTermSa_, nullptr);
    }

    void TTermHandlers::SigHandler(int signum) {
        switch (signum) {
            case SIGINT:
            case SIGTERM:
            case SIGHUP:
                Server_->Shutdown();
                Server_->SetExitCode(TermErrorEC);
                break;
        }
    }

    NUserService::TSingletonServer* volatile TTermHandlers::Server_;

    void SendSignal(const TSystemWideName& stale, int signum) {
        auto pid = stale.GetPid();
        bool ok = kill(pid, signum) == 0;
        if (!ok) {
            switch (errno) {
                case EPERM:
                    ythrow TSystemError() << "Have no permissions to kill " << pid;
                case ESRCH:
                    break;
                default:
                    ythrow TSystemError() << "Unexpected error " << errno << " " << LastSystemErrorText();
            }
        }
    }
}
#endif
