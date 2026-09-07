#include "options.h"
#include "tool_config.h"
#include "toolchain.h"
#include "toolscache.h"

#include <devtools/ya/cpp/lib/snowden/snowden.h>

#include <devtools/libs/yaplatform/platform.h>
#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/ya_handler.h>
#include <devtools/ya/cpp/lib/logger.h>

#include <util/stream/output.h>
#include <util/string/builder.h>
#include <util/string/join.h>
#include <util/generic/yexception.h>

namespace NYa::NTool {
    namespace {
        void ProcessDoubleDashes(TToolInvocation& invocation, bool swallowDoubleDash) {
            auto& opts = invocation.ToolOptions;
            if (opts.empty()) {
                return;
            }
            // Remove the double dash immediately after the tool name.
            if (opts.front() == "--") {
                opts.erase(opts.begin());
                return;
            }
            if (!swallowDoubleDash) {
                return;
            }
            for (auto it = opts.begin(); it != opts.end(); ++it) {
                if (*it == "--") {
                    opts.erase(it);
                    return;
                }
            }
        }
    }

    void ToolFastPath(const TToolOptions& options) {
        const IConfig& config = GetConfig();
        TToolInvocation invocation = LoadToolInvocation(config, options.Args);
        ProcessDoubleDashes(invocation, options.SwallowDoubleDash);
        DEBUG_LOG << "Run tool fast path: " << invocation.ToolName << " " << JoinSeq(" ", invocation.ToolOptions) << "\n";
        DEBUG_LOG << "Cwd: " << TFsPath::Cwd() << "\n";
        DEBUG_LOG << "Home: " << config.HomeDir() << "\n";
        DEBUG_LOG << "Ya dir: " << config.MiscRoot() << "\n";
        DEBUG_LOG << "Tool root: " << config.ToolRoot() << "\n";

        TCanonizedPlatform forPlatform = options.HostPlatform ? TCanonizedPlatform(options.HostPlatform) : MyPlatform();
        DEBUG_LOG << "Platform: '" << forPlatform.AsString() << "'\n";

        NTool::TTool tool = NTool::GetTool(config, invocation.Config, invocation.ToolName, forPlatform);
        DEBUG_LOG << "Tool chain path: '" << tool.ToolChainPath << "'\n";
        DEBUG_LOG << "Tool path: '" << tool.ToolPath << "'\n";

        auto toolsCache = MakeHolder<NTool::TToolsCache>(config);
        toolsCache->Notify(tool.ToolChainPath);

        if (options.PrintToolChainPath) {
            Cout << tool.ToolChainPath << Endl;
            toolsCache->Lock(tool.ToolChainPath);
            return;
        }
        if (options.PrintPath) {
            Cout << tool.ToolPath << Endl;
            toolsCache->Lock(tool.ToolChainPath);
            return;
        }

        toolsCache.Destroy();

        NSnowden::ReportToolExecutionEvent(config, invocation.ToolName, tool.ToolPath.GetPath());

        ExecTool(config, tool, invocation.ToolOptions);
    }

    struct TToolHandler : public IYaHandler {
        void Run(const TVector<TStringBuf>& args) override {
            TToolOptions options{};
            try {
                ParseOptions(options, args);
                ToolFastPath(options);
            } catch (const yexception& e) {
                TStringStream err;
                err << "Tool fast path failed with error: " << e.what() << "\n";
                ERROR_LOG << err.Str();
                if (options.PrintFastPathError) {
                    Cerr << err.Str();
                }
                if (options.NoFallbackToPython) {
                    exit(1);
                }
                return;
            }
            exit(0);
        }
    };

    static TClassRegistrar<IYaHandler, TToolHandler> registration{"tool"};
}
