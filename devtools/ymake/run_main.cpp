#include "run_main.h"

#include "build_result.h"
#include "conf.h"
#include "main.h"
#include "trace_start.h"
#include "ymake.h"
#include "context_executor.h"

#include <asio/use_future.hpp>
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

#include <asio/use_awaitable.hpp>
#include <asio/thread_pool.hpp>
#include <asio/detached.hpp>
#include <asio/co_spawn.hpp>

#if defined(__linux__)
#include <sys/mman.h>
#endif

static void SigInt(int) {
    _Exit(BR_INTERRUPTED);
}

static TVector<TVector<const char*>> SplitMulticonfigCmdline(int argc, char** argv) {
    TVector<TVector<const char*>> configs;

    using namespace std::views;

    // split by --conf-id then drop the very first part: it is the program name or the whole command line
    for (const auto v : split(TVector<const char*>{argv, argv + argc}, TVector<TStringBuf>{"--conf-id"sv}) | drop(1)) {
        TVector<const char*> config{argv[0]};
        // (+ 1) means we omit conf-id value since we don't need it for now
        config.insert(config.end(), v.begin() + 1, v.end());
        configs.push_back(std::move(config));
    }

    if (configs.empty()) {
        configs.push_back(TVector<const char*>(argv, argv + argc));
    }

    return configs;
}

using namespace NLastGetopt;

void InitGlobalOpts(int argc, char** argv) {
    try {
        TVector<TString> events;
        for (int i = 1; i < argc; ++i) {
            TString value = argv[i];
            if (value == "--events" || value == "-E") {
                events.emplace_back(argv[++i]);
            }
        }
        if (!events.empty()) {
            if (!std::all_of(events.begin(), events.end(), [&events](const TString& event) {return event == events.front();})) {
                YWarn() << "All trace events must be the same" << Endl;
            }
            NYMake::InitTraceSubsystem(events.front());
            YDebug() << "Trace events enabled: " << events.front() << Endl;
        }
    } catch (const yexception& error) {
        YErr() << "Global opts initialization failed with error: " << error.what() << Endl;
    }
}

TMaybe<EBuildResult> InitConf(const TVector<const char*>& value, TBuildConfiguration& conf) {
    try {
        TOpts opts;
        opts.ArgPermutation_ = REQUIRE_ORDER;
        opts.AddHelpOption('?');

        conf.AddOptions(opts);

        const TOptsParseResult res(&opts, value.size(), const_cast<const char**>(value.data()));

        // This calls FORCE_TRACE(U, NEvent::TStageStated("ymake run")); after tracing initialization
        conf.PostProcess(res.GetFreeArgs());
    } catch (const yexception& error) {
        YErr() << "Conf initialization failed with error: " << error.what() << Endl;
        return BR_FATAL_ERROR;
    }
    return TMaybe<EBuildResult>();
}

asio::awaitable<int> RunConfigure(TVector<const char*> value, TExecutorWithContext<TExecContext> exec) {
    TBuildConfiguration conf;
    auto result = InitConf(value, conf);
    if (result.Defined()) {
        co_return result.GetRef();
    }

    try {
        LockAllMemory(LockCurrentMemory);
    } catch (const yexception&) {
        YDebug() << "mlockall failed" << Endl;
    }

    int ret_code = BR_OK;
    try {
        FORCE_TRACE(U, NEvent::TStageStarted("ymake main"));
        ret_code = co_await main_real(conf, exec);
        FORCE_TRACE(U, NEvent::TStageFinished("ymake main"));
    } catch (const yexception& error) {
        YErr() << "Configure stage failed with error: " << error.what() << Endl;
        ret_code = BR_FATAL_ERROR;
    }
    co_return ret_code;
}

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
    asio::thread_pool configure_workers(10);

    InitGlobalOpts(argc, argv);

    auto configs = SplitMulticonfigCmdline(argc, argv);
    TVector<std::future<int>> ret_codes(configs.size());
    for (size_t i = 0; i < configs.size(); ++i) {
        auto ctx = std::make_shared<TExecContext>(
            std::make_shared<NCommonDisplay::TLockedStream>(),
            std::make_shared<TConfMsgManager>(),
            std::make_shared<TDiagCtrl>()
        );
        auto proxy = TExecutorWithContext<TExecContext>(
            asio::require(configure_workers.executor(), asio::execution::blocking.never),
            ctx
        );
        ret_codes[i] = asio::co_spawn(proxy, RunConfigure(configs[i], proxy), asio::use_future);
    }

    int main_ret_code = BR_OK;
    for (auto& ret_code : ret_codes) {
        main_ret_code |= ret_code.get();
    }
    configure_workers.wait();

    return main_ret_code;
}
