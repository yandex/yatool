#include "options.h"

#include <util/generic/algorithm.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/system/env.h>

#include <functional>
#include <span>

// NIH:
// 1. library/cpp/getopt doesn't support partial parsing.
// 2. Tool handler parameters is a tricky mix of options:
//    - known for python handler only;
//    - known for both fast path and python handlers;
//    - launched tool options.

namespace NYa::NTool {
    namespace {
        using TBoolToolOptionPtr = bool TToolOptions::*;
        using TStringToolOptionPtr = TString TToolOptions::*;
        using TArgsSpan = std::span<const TStringBuf>;

        const TStringBuf TOOL_HANDLER_NAME = "tool";

        // These options are still allowed after the tool name
        const TStringBuf LEGACY_UNSUPPORTED_OPTIONS[] = {
            "--docker-config-path",
            "--force-refetch",
            "--force-update",
            "--get-resource-id",
            "--key",
            "--platform",
            "--target-platform",
            "--token",
            "--toolchain",
            "--user",
            "--ya-help",
        };
        const std::tuple<TStringBuf, std::variant<TBoolToolOptionPtr, TStringToolOptionPtr>> LEGACY_OPTIONS[] = {
            {"--print-path", &TToolOptions::PrintPath},
            {"--print-toolchain-path", &TToolOptions::PrintToolChainPath},
            {"--host-platform", &TToolOptions::HostPlatform},
            {"--print-fastpath-error", &TToolOptions::PrintFastPathError},
            {"--no-fallback-to-python", &TToolOptions::NoFallbackToPython},
            // This option is deprecated, but we considered supporting it to prevent falling to python if a user specifies it after the tool name.
            // However, we don't want to encourage users to use it before tool name, so it is intentionally not supported in OPTIONS.
            {"--hide-arm64-host-warning", &TToolOptions::Dummy},
        };

        // These options are allowed before the tool name.
        // Any other option before the tool name triggers the fallback to python, so there is no corresponding UNSUPPORTED_OPTIONS list.
        const std::tuple<TStringBuf, std::variant<bool TToolOptions::*, TString TToolOptions::*>> OPTIONS[] = {
            {"--print-path", &TToolOptions::PrintPath},
            {"--print-toolchain-path", &TToolOptions::PrintToolChainPath},
            {"--print-fastpath-error", &TToolOptions::PrintFastPathError},
            {"--no-fallback-to-python", &TToolOptions::NoFallbackToPython},
            {"--host-platform", &TToolOptions::HostPlatform},
        };

        // Trivial option definition: option is either a flag or has a string argument
        struct TOptionDef {
            TOptionDef(const TStringBuf optionName, bool* boolTarget)
                : OptionName{optionName}
                , BoolArgumentTarget(boolTarget)
            {

            }

            TOptionDef(const TStringBuf optionName, TString* stringTarget)
                : OptionName{optionName}
                , StringArgumentTarget(stringTarget)
            {

            }

            bool RequiredArgument() const {
                return StringArgumentTarget;
            }

            void Apply() const {
                Y_ASSERT(!RequiredArgument());
                *BoolArgumentTarget = true;
            }

            void Apply(const TStringBuf value) const {
                Y_ASSERT(RequiredArgument());
                *StringArgumentTarget = value;
            }

            const TString OptionName;
            bool* BoolArgumentTarget = nullptr;
            TString* StringArgumentTarget = nullptr;
        };

        TArgsSpan Parse(const TVector<TOptionDef>& optionDefs, TArgsSpan args, std::function<bool(TStringBuf)> onUnknownArg) {
            for (size_t i = 0; i < args.size(); ++i) {
                const TStringBuf arg = args[i];
                if (arg.StartsWith("-")) {
                    if (arg == "--") {
                        return args.subspan(i + 1);
                    }
                    TStringBuf value = arg;
                    const TStringBuf optionName = value.NextTok('=');
                    const TOptionDef* optionDef = FindIfPtr(optionDefs, [optionName](const TOptionDef& def) {return def.OptionName == optionName;});
                    if (optionDef) {
                        if (optionDef->RequiredArgument()) {
                            if (value.IsInited()) {
                                optionDef->Apply(value);
                            } else {
                                ++i;
                                if (i == args.size()) {
                                    throw yexception() << "option '" << optionName << "' requires an argument";
                                }
                                optionDef->Apply(args[i]);
                            }
                        } else if (value.IsInited()) {
                            throw yexception() << "option '" << optionName << "' must have no argument";
                        } else {
                            optionDef->Apply();
                        }
                        continue;
                    }
                }
                if (!onUnknownArg(arg)) {
                    return args.subspan(i);
                }
            }
            return {};
        }

        TArgsSpan ParseLegacyOptions(TToolOptions& options, TArgsSpan args) {
            TVector<TOptionDef> optionDefs{};
            for (const auto& [name, ptr] : LEGACY_OPTIONS) {
                if (const auto ptrToTypedOptPtr = std::get_if<TBoolToolOptionPtr>(&ptr)) {
                    optionDefs.emplace_back(name, &(options.**ptrToTypedOptPtr));
                } else {
                    std::visit(
                        [&](const auto& typedOptPtr) {
                            optionDefs.emplace_back(name, &(options.*typedOptPtr));
                        },
                        ptr
                    );
                }
            }
            return Parse(
                optionDefs,
                args,
                [&](TStringBuf arg) {
                    for (TStringBuf opt : LEGACY_UNSUPPORTED_OPTIONS) {
                        if (arg == opt || arg.Before('=') == opt) {
                            ythrow yexception() << "Unsupported option is found: '" << arg <<"'";
                        }
                    }
                    options.ToolOptions.push_back(TString(arg));
                    return true;
                }
            );
        }
    }

    void ParseOptions(TToolOptions& options, const TVector<TStringBuf>& args) {
        TArgsSpan curArgs{args};
        // 3: ya + TOOL_HANDLER_NAME + tool_name
        Y_ENSURE(curArgs.size() >= 3, "Too few args");

        options.ProgramName = args[0];
        curArgs = curArgs.subspan(1);
        if (curArgs[0] == "-v" || args[0] == "--verbose") {
            curArgs = curArgs.subspan(1);
            // 2: TOOL_HANDLER_NAME + tool_name
            Y_ENSURE(curArgs.size() >= 2, "Too few args");
        }

        Y_ENSURE(
            curArgs[0] == TOOL_HANDLER_NAME,
            "First arg must be a handler name ('tool') or '-v'/'--verbose' flag"
        );
        curArgs = curArgs.subspan(1);

        TVector<TOptionDef> optionDefs{};
        for (const auto& [name, ptr] : OPTIONS) {
            if (const auto ptrToTypedOptPtr = std::get_if<TBoolToolOptionPtr>(&ptr)) {
                optionDefs.emplace_back(name, &(options.**ptrToTypedOptPtr));
            } else {
                std::visit(
                    [&](const auto& typedOptPtr) {
                        optionDefs.emplace_back(name, &(options.*typedOptPtr));
                    },
                    ptr
                );
            }
        }
        curArgs = Parse(
            optionDefs,
            curArgs,
            [](TStringBuf) {return false;}
        );
        Y_ENSURE(!curArgs.empty() && !curArgs[0].starts_with("-"), "Tool name is missing");

        options.ToolName = curArgs[0];
        curArgs = curArgs.subspan(1);

        curArgs = ParseLegacyOptions(options, curArgs);
        options.ToolOptions.insert(options.ToolOptions.end(), curArgs.begin(), curArgs.end());

        if (!options.HostPlatform) {
            if (TString HostPlatform = GetEnv("YA_TOOL_HOST_PLATFORM")) {
                options.HostPlatform = HostPlatform;
            }
        }
        Y_ENSURE(options.ToolName, "Tool name is missing");
   }

    namespace NTest {
        TVector<TStringBuf> GetLegacyOptions() {
            TVector<TStringBuf> result{};
            for (const auto& name : LEGACY_UNSUPPORTED_OPTIONS) {
                result.push_back(name);
            }
            for (const auto& [name, _] : LEGACY_OPTIONS) {
                result.push_back(name);
            }
            return result;
        }
    }
}

