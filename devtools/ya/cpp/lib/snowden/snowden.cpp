#include "snowden.h"
#include "snowden_private.h"

#include <devtools/ya/cpp/lib/logger.h>

#include <util/generic/yexception.h>
#include <util/stream/null.h>
#include <util/system/env.h>
#include <util/system/execpath.h>
#include <util/system/shellcommand.h>

namespace NYa::NSnowden {
    namespace NPrivate {
        TMaybe<int> RunPythonEntryPoint(
            const TString& executable,
            const TString& entryPoint,
            const TList<TString>& args,
            bool async
        ) {
#if defined(_unix_)
            // Keep fd 0 occupied while TShellCommand creates its output pipes.
            // Otherwise an output pipe can take fd 0 and close the redirected
            // stdin in the child while rearranging descriptors after fork().
            TFileHandle stdinReservation("/dev/null", OpenExisting | RdOnly | CloseOnExec);
            Y_ENSURE(stdinReservation.IsOpen(), "Cannot reserve stdin for Snowden child process");
#endif
            TShellCommandOptions opts;
            TNullInput nullIn;
            opts
                .SetDetachSession(true)
                .SetAsync(async)
                .SetInputStream(&nullIn)
                .SetOutputStream(nullptr)
                .SetErrorStream(nullptr);

            opts.Environment["Y_PYTHON_ENTRY_POINT"] = entryPoint;

            const TString gsid = GetEnv("GSID");
            if (!gsid.empty()) {
                opts.Environment["GSID"] = gsid;
            }

            TShellCommand cmd(executable, args, opts);
            cmd.Run();
            if (async) {
                return Nothing();
            }
            return cmd.GetExitCode();
        }
    }

    namespace {

        void SpawnPythonEntryPoint(const TString& entryPoint, const TList<TString>& args) {
            NPrivate::RunPythonEntryPoint(GetExecPath(), entryPoint, args, true);
        }

    } // namespace

    void EnsureDaemon(const IConfig& /*config*/) {
        try {
            if (GetEnv("YA_SNOWDEN_MODE") != "standalone") {
                return;
            }
            SpawnPythonEntryPoint(
                "yalibrary.snowden:ensure_daemon_main",
                {}
            );
            DEBUG_LOG << "[snowden] EnsureDaemon initiated\n";
        } catch (...) {
            DEBUG_LOG << "[snowden] EnsureDaemon failed silently\n";
        }
    }

    void ReportCppHandlerEvent(const TString& handlerName) {
        try {
            SpawnPythonEntryPoint(
                "yalibrary.snowden:push_event_main",
                {
                    "--key",   "cpp_handler",
                    "--field", TString("handler_name=") + handlerName,
                }
            );
            DEBUG_LOG << "[snowden] CppHandler event push initiated: " << handlerName << "\n";
        } catch (...) {
            DEBUG_LOG << "[snowden] ReportCppHandlerEvent failed silently\n";
        }
    }

    void ReportToolExecutionEvent(
        const IConfig& /*config*/,
        const TString& toolName,
        const TString& toolPath
    ) {
        try {
            SpawnPythonEntryPoint(
                "yalibrary.snowden:push_event_main",
                {
                    "--key",   "tool_execution",
                    "--field", "tool_launch_method=cpp_fast_path",
                    "--field", TString("tool_name=") + toolName,
                    "--field", TString("tool_path=") + toolPath,
                }
            );
            DEBUG_LOG << "[snowden] ToolExecution event push initiated: " << toolName << "\n";
        } catch (...) {
            // Telemetry must never break tool execution.
            DEBUG_LOG << "[snowden] ReportToolExecutionEvent failed silently\n";
        }
    }

} // namespace NYa::NSnowden
