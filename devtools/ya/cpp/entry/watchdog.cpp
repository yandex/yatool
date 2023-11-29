#include "util/system/getpid.h"
#include "util/system/thread.h"
#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <util/system/env.h>
#include <stdexcept>
#include "watchdog.h"

#ifdef _win_
    #define SIGNAL SIGABRT
#else
    #define SIGNAL SIGQUIT
#endif

struct WatchdogOptions {
    TDuration timeout;
    int signal;
};

void Terminate(THolder<WatchdogOptions>& opts) {
#ifdef _win_
    WARNING_LOG << "Raise signal " << opts->signal << Endl;
    raise(opts->signal);
#else
    auto pid = GetPID();
    WARNING_LOG << "Shut down process group " << getpgid(pid) << " with signal " << strsignal(opts->signal) << Endl;
    kill(0, opts->signal);
#endif
}

void RunWatchdogTimer(THolder<WatchdogOptions> opts) {
    INFO_LOG << "Wait for " << opts->timeout << Endl;
    Sleep(opts->timeout);
    Terminate(opts);
}

void* RunWatchdogTimerInThread(void* watchdog_opts) {
    THolder<WatchdogOptions> opts((WatchdogOptions*)watchdog_opts);
    RunWatchdogTimer(std::move(opts));
    return nullptr;
}

void InitWatchdogFromEnv() {
    auto raw_timeout = GetEnv("YA_TIMEOUT");
    if (!raw_timeout) {
        return;
    }
    auto timeout = TDuration::Parse(raw_timeout);

    auto opts = new WatchdogOptions{timeout, SIGNAL};
    TThread t(RunWatchdogTimerInThread, opts);
    t.Start();
    t.Detach();
}
