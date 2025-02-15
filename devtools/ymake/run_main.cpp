#include "run_main.h"

#include "build_result.h"
#include "conf.h"
#include "main.h"
#include "trace_start.h"

#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/stats.h>

#include <library/cpp/sighandler/async_signals_handler.h>
#include <library/cpp/getopt/small/last_getopt.h>

#include <util/generic/fwd.h>
#include <util/generic/yexception.h>
#include <util/system/env.h>
#include <util/system/mlock.h>
#include <util/system/platform.h>

#include <signal.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <sys/mman.h>
#endif

static void SigInt(int) {
    _Exit(BR_INTERRUPTED);
}

using namespace NLastGetopt;

int YMakeMain(int argc, char** argv) {
    TraceYmakeStart(argc, argv);

#if !defined(_win_)
    SetEnv("LC_ALL", "C");
#else
    // Don't print a silly message or stick a modal dialog box in my face,
    // please.
    _set_abort_behavior(0u, ~0u);
#endif // !_MSC_VER

    SetAsyncSignalHandler(SIGINT, SigInt);

    TBuildConfiguration conf;

    try {
        TOpts opts;
        opts.ArgPermutation_ = REQUIRE_ORDER;
        opts.AddHelpOption('?');

        conf.AddOptions(opts);

        const TOptsParseResult res(&opts, argc, argv);

        // This calls FORCE_TRACE(U, NEvent::TStageStated("ymake run")); after tracing initialization
        conf.PostProcess(res.GetFreeArgs());
    } catch (const yexception& error) {
        YErr() << error.what() << Endl;
        return BR_FATAL_ERROR;
    }

    try {
        LockAllMemory(LockCurrentMemory);
    } catch (const yexception&) {
        YDebug() << "mlockall failed" << Endl;
    }

    FORCE_TRACE(U, NEvent::TStageStarted("ymake main"));
    const auto ret_code = main_real(conf);
    FORCE_TRACE(U, NEvent::TStageFinished("ymake main"));

    return ret_code;
}
