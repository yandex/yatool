#include "run_main.h"

#include "build_result.h"
#include "conf.h"
#include "main.h"
#include "trace_start.h"
#include "context_executor.h"

#include <asio/use_future.hpp>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/progress_manager.h>
#include <devtools/ymake/diag/stats.h>
#include <devtools/ymake/foreign_platforms/pipeline.h>
#include <devtools/ymake/python_runtime.h>

#include <library/cpp/getopt/small/last_getopt.h>
#include <library/cpp/iterator/enumerate.h>
#include <library/cpp/sighandler/async_signals_handler.h>

#include <util/generic/algorithm.h>
#include <util/generic/queue.h>
#include <util/generic/scope.h>
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

namespace {

void SigInt(int) {
    _Exit(BR_INTERRUPTED);
}

#if !defined(_win_)
void PrintBackTraceOnSignal(int signum, siginfo_t*, void*) {
    Cerr << "Signal " << signum << ", backtrace is:" << Endl;
    PrintBackTrace();
    raise(signum);
}

using THandler = void (*)(int, siginfo_t*, void*);
void SetupSignalHandler(int signum, THandler handler) {
    struct sigaction sa = {};
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESETHAND,
    sa.sa_sigaction = handler;
    Y_ENSURE(sigaction(signum, &sa, nullptr) == 0, strerror(errno));
}
#endif // !_win_

struct TConfigDesc {
    size_t Id;
    TVector<const char*> CmdLine;
};

TVector<TVector<const char*>> SplitMulticonfigCmdline(int argc, char** argv) {
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

const char* GetOption(int& argc, char** argv, std::string_view option) noexcept {
    const char* result = nullptr;
    int out = 1;

    for (int in = 1; in < argc; ) {
        if (!result && argv[in] == option && in + 1 < argc && argv[in + 1][0] != '-') {
            result = argv[in + 1];
            in += 2;
        } else {
            argv[out++] = argv[in++];
        }
    }

    argc = out;
    return result;
}

void InitGlobalOpts(int& argc, char** argv, int& threads, bool& useSubinterpreters) {
    try {
        TVector<TString> events;
        for (const auto& name : { "--events", "-E" }) {
            while (true) {
                const char* value = GetOption(argc, argv, name);
                if (value == nullptr)
                    break;
                events.push_back(std::move(value));
            }
        }

        for (const auto& name : { "--threads", "-t" }) {
            const char* threadsStr = GetOption(argc, argv, name);
            if (threadsStr != nullptr) {
                threads = std::stoul(threadsStr);
            }
        }

        useSubinterpreters = FindPtr(argv, argv + argc, TStringBuf{"--use-subinterpreters"});

        if (!events.empty()) {
            if (!std::all_of(events.begin(), events.end(), [&events](const TString& event) {return event == events.front();})) {
                YWarn() << "All trace events must be the same" << Endl;
            }
            NYMake::InitTraceSubsystem(events.front());
        }
    } catch (const yexception& error) {
        YErr() << "Global opts initialization failed with error: " << error.what() << Endl;
    }
}

TMaybe<EBuildResult> InitConf(const TVector<const char*>& value, TBuildConfiguration& conf, NForeignTargetPipeline::TForeignTargetPipeline& pipeline) {
    try {
        TOpts opts;
        opts.ArgPermutation_ = REQUIRE_ORDER;
        opts.AddHelpOption('?');

        conf.AddOptions(opts);

        const TOptsParseResult res(&opts, value.size(), const_cast<const char**>(value.data()));

        // Create writer as early as possible to notify readers on destruction.
        conf.ForeignTargetWriter = pipeline.CreateWriter(conf);

        // This calls FORCE_TRACE(U, NEvent::TStageStated("ymake run")); after tracing initialization
        conf.PostProcess(res.GetFreeArgs());

        // Readers may require input stream to be set.
        conf.ForeignTargetReader = pipeline.CreateReader(conf);
    } catch (const yexception& error) {
        YErr() << "Conf initialization failed with error: " << error.what() << Endl;
        return BR_FATAL_ERROR;
    }
    return TMaybe<EBuildResult>();
}

// Attempt to call MLock after logger is ready leads to mlock failure thus it's called before
// logger initialization and it's failure is reported later. This var keeps mlock call result.
bool MLOCK_FAILED = false;

asio::awaitable<int> RunConfigure(TVector<const char*> value, PyInterpreterState* interp, TExecutorWithContext<TExecContext> exec, NForeignTargetPipeline::TForeignTargetPipeline& pipeline) {
    TBuildConfiguration conf;
    TMaybe<EBuildResult> result{};
    {
        NYMake::TPythonThreadStateScope initState{interp};
        result = InitConf(value, conf, pipeline);
    }

    Y_DEFER {
        NYMake::TPythonThreadStateScope finiState{interp};
        conf.ClearPlugins();
    };

    if (result.Defined()) {
        co_return result.GetRef();
    }
    conf.SubState = interp;

    if (MLOCK_FAILED) {
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

}

auto CreatePipeline(const TVector<TVector<const char*>>& configs , asio::any_io_executor exec) {
    THolder<NForeignTargetPipeline::TForeignTargetPipeline> pipeline;
    const bool hasMulticonfig = configs.size() > 1;
    size_t marked = 0;
    if (hasMulticonfig) {
        pipeline = MakeHolder<NForeignTargetPipeline::TForeignTargetPipelineInternal>(exec);
        for (const auto& config : configs) {
            marked += pipeline->RegisterConfig(config);
        }
    }
    constexpr size_t confsCanHaveNoMarks = 1; // tool config can have no mark yet
    const bool hasAllRequiredMarks = marked >= configs.size() - confsCanHaveNoMarks;
    if (!hasMulticonfig || !hasAllRequiredMarks) {
        pipeline = MakeHolder<NForeignTargetPipeline::TForeignTargetPipelineExternal>();
    }
    return pipeline;
}

using TChannel = asio::experimental::concurrent_channel<void(asio::error_code, int)>;

void SubmitNextConfigIfAny(NYMake::TPythonRuntimeScope& pythonRuntime, TAdaptiveLock& confQueueLock, TQueue<TConfigDesc>& confQueue, asio::thread_pool::executor_type exec, THolder<NForeignTargetPipeline::TForeignTargetPipeline>& pipeline, TDeque<TAtomicSharedPtr<TChannel>>& channels) {
    TConfigDesc config;
    with_lock(confQueueLock) {
        if (confQueue.empty()) {
            return;
        }
        config = confQueue.front();
        confQueue.pop();
    }
    auto ctx = std::make_shared<TExecContext>(
        std::make_shared<NCommonDisplay::TLockedStream>(),
        std::make_shared<TConfMsgManager>(),
        std::make_shared<TProgressManager>(),
        std::make_shared<TDiagCtrl>()
    );
    auto proxy = TExecutorWithContext<TExecContext>(
        asio::require(exec, asio::execution::blocking.never),
        ctx
    );
    auto p = MakeAtomicShared<TChannel>(exec, 1u);
    with_lock(confQueueLock) {
        channels.push_back(p); // TODO: mb other lock
    }
    asio::co_spawn(proxy, RunConfigure(config.CmdLine, pythonRuntime.GetSubinterpreterState(config.Id), proxy, *pipeline), [p, &pythonRuntime, &confQueueLock, &confQueue, exec, &pipeline, &channels](std::exception_ptr ptr, int rc) {
        if (ptr) {
            p->cancel();
            std::rethrow_exception(ptr);
        } else {
            SubmitNextConfigIfAny(pythonRuntime, confQueueLock, confQueue, exec, pipeline, channels);
            p->async_send({}, rc, asio::detached);
        }
    });
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
#if !defined(_win_)
    SetupSignalHandler(SIGSEGV, PrintBackTraceOnSignal);
    SetupSignalHandler(SIGABRT, PrintBackTraceOnSignal);
#endif // !_win_

    int threads = 0;
    bool useSubinterpreters = false;
    InitGlobalOpts(argc, argv, threads, useSubinterpreters);

    auto configs = SplitMulticonfigCmdline(argc, argv);
    TVector<std::future<int>> ret_codes(configs.size());
    if (threads <= 0) {
        threads = configs.size() + 1;
    }

    try {
        LockAllMemory(LockCurrentMemory);
    } catch (const yexception&) {
        MLOCK_FAILED = true;
    }

    NYMake::TPythonRuntimeScope pythonRuntime(useSubinterpreters, configs.size());

    asio::thread_pool configure_workers(threads);
    auto pipeline = CreatePipeline(configs, configure_workers.executor());

    TDeque<TAtomicSharedPtr<TChannel>> channels;
    TAdaptiveLock confQueueLock;
    TQueue<TConfigDesc> confQueue;
    for (const auto& [i, config] : Enumerate(configs)) {
        confQueue.emplace(i, config);
    }

    for (int i = 0; i < threads; ++i) {
        SubmitNextConfigIfAny(pythonRuntime, confQueueLock, confQueue, configure_workers.executor(), pipeline, channels);
    }

    int main_ret_code = BR_OK;
    for (size_t i = 0; i < channels.size(); ++i) {
        main_ret_code |= channels[i]->async_receive(asio::use_future).get();
    }
    configure_workers.wait();

    return main_ret_code;
}
