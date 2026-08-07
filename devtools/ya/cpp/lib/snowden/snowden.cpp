#include "snowden.h"

#include <devtools/ya/cpp/lib/logger.h>

#include <util/system/env.h>
#include <util/system/execpath.h>
#include <util/system/shellcommand.h>

namespace NYa::NSnowden {
    namespace {

        void SpawnPythonEntryPoint(const TString& entryPoint, const TList<TString>& args) {
            TShellCommandOptions opts;
            opts
                .SetDetachSession(true)
                .SetAsync(true)
                .SetOutputStream(nullptr)
                .SetErrorStream(nullptr);

            opts.Environment["Y_PYTHON_ENTRY_POINT"] = entryPoint;

            const TString gsid = GetEnv("GSID");
            if (!gsid.empty()) {
                opts.Environment["GSID"] = gsid;
            }

            TShellCommand cmd(GetExecPath(), args, opts);
            cmd.Run();
        }

    } // namespace

    void EnsureDaemon(const IConfig& /*config*/) {
        try {
            if (GetEnv("YA_SNOWDEN_MODE") != "standalone") {
                return;
            }
            SpawnPythonEntryPoint(
                "devtools.ya.yalibrary.snowden:ensure_daemon_main",
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
                "devtools.ya.yalibrary.snowden:push_event_main",
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
                "devtools.ya.yalibrary.snowden:push_event_main",
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
