#pragma once

#include "server.h"

#if defined(_unix_)
#include <util/system/sigset.h>

namespace NUserService {
    struct TSuspendSignals {
        TSuspendSignals(bool ignoreErrors) {
            SigEmptySet(&Oldmask);

            sigset_t newmask;
            Y_ENSURE_EX(SigFillSet(&newmask) == 0 || ignoreErrors, TWithBackTrace<yexception>());
            Y_ENSURE_EX(SigProcMask(SIG_SETMASK, &newmask, &Oldmask) == 0 || ignoreErrors, TWithBackTrace<yexception>());
        }
        ~TSuspendSignals() {
            SigProcMask(SIG_SETMASK, &Oldmask, nullptr);
        }
        sigset_t Oldmask;
    };

    class TTermHandlers {
    public:
        TTermHandlers(NUserService::TSingletonServer* server, TLog& log) {
            SetHandlers(server, log);
        }
        ~TTermHandlers() {
            ResetHandlers();
        }

    private:
        void SetHandlers(NUserService::TSingletonServer* server, TLog& log);
        void ResetHandlers();
        static void SigHandler(int signal);

    private:
        struct sigaction OldiTermSa_;
        struct sigaction OldiHupSa_;

        static NUserService::TSingletonServer* volatile Server_;
    };

    void SendSignal(const TSystemWideName& stale, int signum);
}
#endif
