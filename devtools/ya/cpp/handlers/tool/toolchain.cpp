#include "toolchain.h"
#include "toolchain_helpers.h"
#include "toolscache.h"

#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <devtools/ya/cpp/lib/process.h>

#include <util/string/join.h>

#ifndef _win_
    #include <util/system/env.h>

    #include <sys/resource.h>
#endif

namespace NYa::NTool {
// Search path separator
#ifdef _win_
    constexpr char PATHSEP = ';';
#else
    constexpr char PATHSEP = ':';
#endif

    TTool GetTool(const IConfig& config, const TString& toolName, const TCanonizedPlatform& forPlatform) {

        TVector<IToolChainPathGetter*> toolChainPathGetters = TSingletonClassFactory<IToolChainPathGetter>::Get()->GetAllObjects();

        const TFsPath toolRoot = config.ToolRoot();
        if (!toolRoot.Exists()) {
            throw yexception() << "Tool root doesn't exist: " << toolRoot;
        }

        const NYaConfJson::TYaConf& yaConf = config.YaConf();

        const NYaConfJson::TToolChain& toolChain = NPrivate::ResolveTool(yaConf, toolName);

        const NYaConfJson::TToolChainTool& toolChainTool = toolChain.Tools.at(toolName);
        TString bottleName = toolChainTool.Bottle.GetRef();
        const NYaConfJson::TBottle& bottle = yaConf.Bottles.at(bottleName);

        TFsPath toolChainPath = NPrivate::GetToolChainPath(config, toolRoot, toolChainPathGetters, bottle, forPlatform);
        TFsPath toolPath = NPrivate::GetToolPath(toolChainPath, toolName, bottle, toolChainTool);

        return TTool{toolName, toolChainPath, toolPath, toolChain.Env};
    }

    void ExecTool(const IConfig& config, const TTool& tool, TVector<TString> toolOptions, TExecve execve) {
        // Remove environment variables set by 'ya' wrapper.
        // They are actually one-time ya-bin parameters rather than inheritable environment
        // for all descendant processes.
        auto filter = [](const TString& key, const TString&) {
            return !(key == "RESPAWNS_PARAM" or key == "YA_SOURCE_ROOT" or key == "YA_PYVER_SET_FORCED" or key == "YA_PYVER_REQUIRE");
        };
        THashMap<TString, TString> env = Environ(filter);

        env["YA_TOOL"] = tool.ToolPath;

        if (tool.Env) {
            for (const auto& [key, val] : tool.Env.GetRef()) {
                TString realVal = JoinSeq(PATHSEP, val);
                SubstGlobal(realVal, "$(ROOT)", tool.ToolChainPath.GetPath());
                env[key] = realVal;
            }
        }
#ifndef _win_
        if (tool.ToolName == "gdb") {
            // gdb does not fit in 8 MB stack with large cores (DEVTOOLS-5040).
            struct rlimit limits{};
            getrlimit(RLIMIT_STACK, &limits);
            rlim_t newLimit = 128 << 20;
            if (limits.rlim_max != RLIM_INFINITY) {
                newLimit = Min(limits.rlim_max, newLimit);
            }
            if (newLimit > limits.rlim_cur) {
                limits.rlim_cur = newLimit;
            }
            setrlimit(RLIMIT_STACK, &limits);
            const auto arcadia_root = TryGetEnv("YA_TOOL_GDB_ARCADIA_ROOT");
            if (const auto& root = arcadia_root ? TFsPath(*arcadia_root) : config.ArcadiaRoot()) {
                toolOptions.insert(toolOptions.begin(), {"-ex", "set substitute-path /-S/ " + root.GetPath() + "/"});
                toolOptions.insert(toolOptions.begin(), {"-ex", "set filename-display absolute"});
            }
        }
#endif

        execve(tool.ToolPath, toolOptions, env, {});
    }
}
