#include "tool_config.h"

#include <devtools/ya/cpp/lib/edl/json/from_json.h>
#include <devtools/ya/cpp/lib/ya_conf_json_loaders.h>

#include <util/string/join.h>
#include <util/string/split.h>

namespace NYa::NTool {
    namespace {
        constexpr TStringBuf TOOL_CONFIG_ROOT = "tools/tools";
        constexpr TStringBuf TOOLCHAIN_CONFIG_ROOT = "tools/toolchains";
        constexpr TStringBuf TOOL_CONFIG_SUFFIX = ".tool.json";
        constexpr TStringBuf TOOLCHAIN_CONFIG_SUFFIX = ".toolchain.json";
        constexpr TStringBuf TOOL_TYPE_PARENT = "parent";
        constexpr TStringBuf TOOL_TYPE_SIMPLE = "simple";
        constexpr TStringBuf TOOL_TYPE_TOOLCHAIN = "toolchain";
        constexpr TStringBuf TOOL_AVAILABILITY_INTERNAL = "internal";

        TFsPath MakeToolConfigPath(const TVector<TString>& nameParts) {
            TFsPath result{TOOL_CONFIG_ROOT};
            for (size_t i = 0; i + 1 < nameParts.size(); ++i) {
                result /= nameParts[i];
            }
            result /= nameParts.back() + TOOL_CONFIG_SUFFIX;
            return result;
        }

        NYaConfJson::TToolConfig LoadToolConfig(TStringBuf data) {
            NYaConfJson::TToolConfig result;
            NEdl::LoadJson(data, result);
            return result;
        }

        void AddToolChain(
            NYaConfJson::TYaConf& result,
            const TString& toolName,
            const TString& toolChainName,
            const NYaConfJson::TToolChain& toolChain,
            const NYaConfJson::TBottles& bottles) {
            const auto* tool = toolChain.Tools.FindPtr(toolName);
            if (!tool || result.ToolChains.contains(toolChainName)) {
                return;
            }

            result.ToolChains.emplace(toolChainName, toolChain);
            if (tool->Bottle) {
                const TString& bottleName = tool->Bottle.GetRef();
                const auto* bottle = bottles.FindPtr(bottleName);
                if (!bottle) {
                    throw yexception() << "Bottle '" << bottleName << "' is missing for toolchain '" << toolChainName << "'";
                }
                result.Bottles.emplace(bottleName, *bottle);
            }
        }

        NYaConfJson::TYaConf LoadToolChainConfig(const IConfig& config, const TString& toolName) {
            NYaConfJson::TYaConf result;

            for (const TString& fileName : config.ListToolConfigs(TOOLCHAIN_CONFIG_ROOT)) {
                if (!fileName.EndsWith(TOOLCHAIN_CONFIG_SUFFIX)) {
                    continue;
                }

                const TFsPath path = TFsPath{TOOLCHAIN_CONFIG_ROOT} / fileName;
                const TMaybe<TString> data = config.ReadToolConfig(path);
                if (!data) {
                    throw yexception() << "Toolchain config disappeared while reading: " << path;
                }

                NYaConfJson::TToolchainsConfig toolchainsConfig;
                NEdl::LoadJson(data.GetRef(), toolchainsConfig);
                toolchainsConfig.Finish();
                for (const auto& [toolChainName, toolChain] : toolchainsConfig.ToolChains) {
                    AddToolChain(result, toolName, toolChainName, toolChain, toolchainsConfig.Bottles);
                }
            }

            const NYaConfJson::TYaConf& legacy = config.YaConf();
            for (const auto& [toolChainName, toolChain] : legacy.ToolChains) {
                AddToolChain(result, toolName, toolChainName, toolChain, legacy.Bottles);
            }
            result.Finish();
            return result;
        }

        TVector<TString> FormulaPlatforms(const IConfig& config, const NYaConfJson::TFormula& formula) {
            const NYaConfJson::TFormula* resolvedFormula = &formula;
            if (const auto* path = std::get_if<TString>(&formula)) {
                resolvedFormula = &config.YaConfFormula(*path);
            }

            const auto* byPlatform = std::get_if<NYaConfJson::TByPlatformFormula>(resolvedFormula);
            if (!byPlatform) {
                return {};
            }

            TVector<TString> result;
            result.reserve(byPlatform->ByPlatform.size());
            for (const auto& [platform, _] : byPlatform->ByPlatform) {
                result.push_back(platform);
            }
            return result;
        }

        NYaConfJson::TYaConf MakeSimpleToolConfig(
            const IConfig& config,
            const TVector<TString>& nameParts,
            const NYaConfJson::TToolDescription& tool) {
            const TString toolName = JoinSeq(' ', nameParts);
            const TString bottleName = JoinSeq('.', nameParts);
            const NYaConfJson::TSimpleToolDefinition definition = tool.Definition.GetOrElse({});

            NYaConfJson::TFormula formula;
            if (definition.Formula) {
                formula = definition.Formula.GetRef();
            } else {
                formula = Join('/', "build/external_resources", JoinSeq('/', nameParts), "resources.json");
            }

            TVector<TString> platforms;
            if (definition.Platforms) {
                platforms = definition.Platforms.GetRef();
            } else {
                platforms = FormulaPlatforms(config, formula);
            }
            if (platforms.empty()) {
                throw yexception() << "Allowed platforms are not specified or empty for tool '" << toolName << "'";
            }

            TVector<TString> executable{nameParts.back()};
            if (definition.Executable) {
                const auto& value = definition.Executable.GetRef();
                if (const auto* path = std::get_if<TString>(&value)) {
                    executable = {*path};
                } else {
                    executable = std::get<TVector<TString>>(value);
                }
            }

            NYaConfJson::TToolChainPlatforms toolChainPlatforms;
            for (const TString& platform : platforms) {
                const TVector<TString> parts = StringSplitter(platform).Split('-');
                if (parts.empty() || parts.size() > 2) {
                    throw yexception() << "Unsupported platform '" << platform << "' for tool '" << toolName << "'";
                }
                TString os = parts[0];
                os.to_upper();
                toolChainPlatforms.push_back({
                    .Host = {
                        .Os = os,
                        .Arch = parts.size() == 2 ? parts[1] : TString{},
                    },
                    .Default = true,
                });
            }

            NYaConfJson::TYaConf result{
                .Bottles = {
                    {bottleName, {
                                     .Name = bottleName,
                                     .Formula = formula,
                                     .Executable = NYaConfJson::TBottleExecutableMap{{toolName, executable}},
                                 }},
                },
                .ToolChains = {
                    {toolName, {
                                   .Name = toolName,
                                   .Tools = {
                                       {toolName, {
                                                      .Bottle = bottleName,
                                                      .Executable = toolName,
                                                  }},
                                   },
                                   .Platforms = toolChainPlatforms,
                                   .Env = definition.Env,
                               }},
                },
            };
            return result;
        }
    }

    TToolInvocation LoadToolInvocation(const IConfig& config, const TVector<TString>& args) {
        if (args.empty()) {
            throw yexception() << "Tool name is missing";
        }

        TVector<TString> nameParts;
        TMaybe<NYaConfJson::TToolDescription> newTool;
        size_t consumed = 0;
        while (consumed < args.size()) {
            if (TStringBuf{args[consumed]}.StartsWith("-")) {
                throw yexception() << "Tool name is expected, got '" << args[consumed] << "'";
            }
            nameParts.push_back(args[consumed++]);

            const TMaybe<TString> data = config.ReadToolConfig(MakeToolConfigPath(nameParts));
            if (!data) {
                // In the split tool configuration this tool is missing.
                // Fallback to the legacy config.
                if (nameParts.size() == 1) {
                    break;
                }
                throw yexception() << "Cannot find tool '" << JoinSeq(' ', nameParts) << "'";
            }

            NYaConfJson::TToolConfig toolConfig = LoadToolConfig(data.GetRef());
            if (toolConfig.Tool.Type == TOOL_TYPE_PARENT) {
                if (consumed == args.size()) {
                    throw yexception() << "Parent tool requires a child: '" << JoinSeq(' ', nameParts) << "'";
                }
                continue;
            }
            if (toolConfig.Tool.Type != TOOL_TYPE_SIMPLE && toolConfig.Tool.Type != TOOL_TYPE_TOOLCHAIN) {
                throw yexception() << "Unknown tool type '" << toolConfig.Tool.Type << "'";
            }
            newTool = std::move(toolConfig.Tool);
            break;
        }

        const TString toolName = JoinSeq(' ', nameParts);
        NYaConfJson::TYaConf toolConfig;
        if (!newTool) {
            toolConfig = config.YaConf();
        } else {
            if (newTool->Availability == TOOL_AVAILABILITY_INTERNAL) {
                throw yexception() << "Tool '" << toolName << "' is for internal use only";
            }
            if (newTool->Type == TOOL_TYPE_SIMPLE) {
                toolConfig = MakeSimpleToolConfig(config, nameParts, newTool.GetRef());
            } else {
                toolConfig = LoadToolChainConfig(config, toolName);
            }
        }


        return {
            .ToolName = toolName,
            .ToolOptions = TVector<TString>{args.begin() + consumed, args.end()},
            .Config = std::move(toolConfig),
        };
    }
}
