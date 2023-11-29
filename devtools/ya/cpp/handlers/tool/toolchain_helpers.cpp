#include "toolchain_helpers.h"

#include <library/cpp/json/writer/json.h>

#include <util/stream/file.h>
#include <util/system/file.h>

namespace NYa::NTool::NPrivate {
    const int CACHE_VERSION = 2;

    const NYaConfJson::TToolChain& ResolveTool(const NYaConfJson::TYaConf& config, TString toolName) {
        constexpr int OTHER_TOOL_PRIORITY = 1;
        constexpr int NATIVE_TOOL_PRIORITY = 2;
        int toolChainPriority = 0;  // The more the better
        const NYaConfJson::TToolChain* foundToolChain{};
        TString currentOs = CurrentOs();

        for (const auto& [name, toolChain] : config.ToolChains) {
            if (toolChain.Tools.contains(toolName) && toolChain.Tools.at(toolName).Bottle) {
                for (const NYaConfJson::TToolChainPlatform& platform : toolChain.Platforms) {
                    if (platform.Default) {
                        if (platform.Host.Os == currentOs) {
                            if (platform.Target && platform.Target->Os == currentOs) {
                                return toolChain; // Best match. Return immediately
                            } else {
                                if (toolChainPriority < NATIVE_TOOL_PRIORITY) {
                                    toolChainPriority = NATIVE_TOOL_PRIORITY;
                                    foundToolChain = &toolChain;
                                }
                            }

                        }
                        else {
                            if (toolChainPriority < OTHER_TOOL_PRIORITY) {
                                toolChainPriority = OTHER_TOOL_PRIORITY;
                                foundToolChain = &toolChain;
                            }
                        }
                    }
                }
            }
        }
        if (foundToolChain) {
            return *foundToolChain;
        } else {
            throw yexception() << "No toolchain found for tool '" << toolName << "'";
        }
    }

    TFsPath GetToolChainPath(
        const IConfig& config,
        const TFsPath& toolRoot,
        const TVector<IToolChainPathGetter*>& toolChainPathGetters,
        const NYaConfJson::TBottle& bottle,
        const TCanonizedPlatform& forPlatform
    ) {
        const NJson::TJsonValue* formulaJson = &bottle.Formula;
        if (formulaJson->IsString()) {
            TFsPath formulaPath = formulaJson->GetString();
            try {
                formulaJson = &config.YaConfFormula(formulaPath); // It's safe to use a raw pointer because formulas are stored in the config reader internal cache
            } catch (const yexception& e) {
                throw yexception() << "Cannot read formula from '" << formulaPath << "': " << e.what();
            }
        }
        TFsPath toolChainPath;
        for (const auto& getter : toolChainPathGetters) {
            toolChainPath = getter->GetPath(toolRoot, bottle, *formulaJson, forPlatform);
            if (toolChainPath) {
                break;
            }
        }
        if (!toolChainPath) {
            throw yexception() << "Unsupported formula: " << NJsonWriter::TBuf().WriteJsonValue(&bottle.Formula).Str();
        }
        if (!toolChainPath.Exists()) {
            throw yexception() << "Tool chain path doesn't exist: " << toolChainPath;
        }

        TFsPath guardPath = toolChainPath / "INSTALLED";
        if (!guardPath.Exists()) {
            throw yexception() << "Guard file doesn't exist: " << guardPath;
        }
        TString cacheVersion = TFileInput(TFile(guardPath, OpenExisting | RdOnly)).ReadAll();
        if (FromString<int>(cacheVersion) < CACHE_VERSION) {
            throw yexception() << "Unsupported cache version: " << cacheVersion;
        }
        return toolChainPath;
    }

    TFsPath GetToolPath(const TFsPath& toolChainPath, const TString& toolName, const NYaConfJson::TBottle& bottle, const NYaConfJson::TToolChainTool& toolChainTool) {
        if (!toolChainTool.Executable || !bottle.Executable) {
            return toolChainPath / toolName;
        }

        TFsPath toolPath = toolChainPath;
        const TVector<TString> pathItems = bottle.Executable->at(toolChainTool.Executable.GetRef());
        for (const TString& pathItem : pathItems) {
            toolPath /= pathItem;
        }
        return toolPath;
    }
}
