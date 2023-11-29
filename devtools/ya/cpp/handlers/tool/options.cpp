#include "options.h"

#include <util/generic/algorithm.h>
#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/system/env.h>

// NIH:
// 1. library/cpp/getopt doesn't support partial parsing.
// 2. Tool handler parameters is a tricky mix of options:
//    - known for python handler only;
//    - known for both fast path and python handlers;
//    - launched tool options.

namespace NYa::NTool {
    const TStringBuf TOOL_HANDLER_NAME = "tool";

    const TStringBuf UNSUPPORTED_OPTIONS[] = {
        "--custom-fetcher",
        "--fetcher-params",
        "--force-refetch",
        "--force-update",
        "--get-param",
        "--get-resource-id",
        "--get-task-id",
        "--hide-arm64-host-warning",
        "--key",
        "--noya-tc",
        "--platform",
        "--print-toolchain-sys-libs",
        "--target-platform",
        "--token",
        "--toolchain",
        "--tools-cache-size",
        "--user",
        "--ya-ac",
        "--ya-ac-conf",
        "--ya-ac-master",
        "--ya-gl-conf",
        "--ya-help",
        "--ya-tc",
        "--ya-tc-bin",
        "--ya-tc-conf",
        "--ya-tc-ini",
        "--ya-tc-master",
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

        void Apply() {
            Y_ASSERT(!RequiredArgument());
            *BoolArgumentTarget = true;
        }

        void Apply(const TStringBuf value) {
            Y_ASSERT(RequiredArgument());
            *StringArgumentTarget = value;
        }

        const TString OptionName;
        bool* BoolArgumentTarget = nullptr;
        TString* StringArgumentTarget = nullptr;
    };

   void ParseOptions(TToolOptions& options, const TVector<TStringBuf>& args) {
        Y_ENSURE(args.size() > 1, "Too few args");

        bool verbose = args[1] == "-v" || args[1] == "--verbose";
        if (verbose) {
            Y_ENSURE(args.size() > 2, "Too few args");
        }
        Y_ENSURE(
            args[1 + static_cast<int>(verbose)] == TOOL_HANDLER_NAME,
            "First arg must be a handler name ('tool') or '-v'/'--verbose' flag"
        );

        size_t toolHandlerIndex = std::distance(args.begin(), std::find(args.begin(), args.end(), TOOL_HANDLER_NAME));

        TVector<TOptionDef> optionDefs{};
        optionDefs.emplace_back("--print-path", &options.PrintPath);
        optionDefs.emplace_back("--print-toolchain-path", &options.PrintToolChainPath);
        optionDefs.emplace_back("--print-fastpath-error", &options.PrintFastPathError);
        optionDefs.emplace_back("--no-fallback-to-python", &options.NoFallbackToPython);
        optionDefs.emplace_back("--host-platform", &options.HostPlatform);

        options.ProgramName = args[0];
        for (size_t i = toolHandlerIndex + 1; i < args.size(); ++i) {
            const TStringBuf arg = args[i];
            if (arg.StartsWith("-")) {
                TStringBuf value = arg;
                const TStringBuf optionName = value.NextTok('=');
                if (Find(UNSUPPORTED_OPTIONS, optionName) != end(UNSUPPORTED_OPTIONS)) {
                    // We don't care unsupported option may have a value and its value falls into the options.ToolOptions
                    options.UnsupportedOption = optionName;
                } else {
                    TOptionDef* optionDef = FindIfPtr(optionDefs, [optionName](const TOptionDef& def) {return def.OptionName == optionName;});
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
                    } else {
                        options.ToolOptions.emplace_back(arg);
                    }
                }
            } else {
                if (!options.ToolName) {
                    options.ToolName = arg;
                } else {
                    options.ToolOptions.emplace_back(arg);
                }
            }
        }
        if (!options.HostPlatform) {
            if (TString HostPlatform = GetEnv("YA_TOOL_HOST_PLATFORM")) {
                options.HostPlatform = HostPlatform;
            }
        }
        Y_ENSURE(!options.UnsupportedOption, TString("Unsupported option is found: '") + options.UnsupportedOption + "'");
        Y_ENSURE(options.ToolName, "Tool name is missing");
   }

    namespace NTest {
        TVector<TStringBuf> GetUnsupportedOptions() {
            return TVector<TStringBuf>(begin(UNSUPPORTED_OPTIONS), end(UNSUPPORTED_OPTIONS));
        }
    }
}

