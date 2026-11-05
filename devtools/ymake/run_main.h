#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <cstddef>
#include <functional>

class IMakePlanSink;

namespace NYMake {
    class TPythonRuntime;

    using TConfigureThreadCount = std::size_t;
    using TConfigureExitCode = int;

    enum class EConfigurationMode { Transition,
                                    Independent };

    struct TConfigureInvocation {
        // Includes argv[0]. Register target nopic, target pic, then host tools.
        // Independent invocations do not participate in platform transitions.
        TVector<TString> Arguments;
        IMakePlanSink* Sink = nullptr;
        // Runs on a configuration worker at the same point as the CLI cache
        // revision event, including saves preceding a later configuration error.
        std::function<void()> OnCacheSaved;
        // Raw stderr text, including serialized ymake events; invoked on the
        // owning configuration worker.
        std::function<void(const TString&)> OnConfigureStderr;
        EConfigurationMode Mode = EConfigurationMode::Transition;
    };

    struct TConfigureRequest {
        TVector<TConfigureInvocation> Configurations;
        TConfigureThreadCount Threads = 0;
    };

    struct TConfigureResult {
        TVector<TConfigureExitCode> ExitCodes;
    };

    // Synchronous library boundary: owns and joins standalone-Asio workers.
    // The caller owns signals and process shutdown. All sinks must outlive
    // the call. Only one Configure operation may be active in a process.
    TConfigureResult Configure(TConfigureRequest request);
    // Runtime must outlive this call; all workers are joined before returning.
    TConfigureResult Configure(TConfigureRequest request, TPythonRuntime& pythonRuntime);
} // namespace NYMake

int YMakeMain(int argc, char** argv);
