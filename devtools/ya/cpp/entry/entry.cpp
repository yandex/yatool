#include <cstddef>
#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/ya_handler.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <devtools/ya/cpp/lib/pgroup.h>
#include <devtools/ya/cpp/entry/watchdog.h>

#include <util/folder/path.h>
#include "util/generic/fwd.h"
#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/system/env.h>
#include <utility>

#ifdef _win_
#include <util/charset/wide.h>  // WideToUTF8, UTF8ToWide
#include <cwchar>               // wcslen
#endif

namespace NYa {
    __attribute__((weak)) void InitYt(int, char**) {
    }

    namespace {
        // Pointer to main function used by arcadia python:
        // Python2: https://a.yandex-team.ru/arcadia/library/python/runtime/main/main.c?rev=f778e4e75ab5665f92abd1466726f4e5d51d4029#L42
        // Python3: https://a.yandex-team.ru/arcadia/library/python/runtime_py3/main/main.c?rev=f778e4e75ab5665f92abd1466726f4e5d51d4029#L231
        // On Windows mainptr has type int(*)(int, wchar_t**), on other platforms int(*)(int, char**).
        // TMain must match exactly to avoid UB when assigning Entry to mainptr.
#ifdef _win_
        using TMain = int (*)(int argc, wchar_t** argv);
#else
        using TMain = int (*)(int argc, char** argv);
#endif
        extern "C" TMain mainptr;
        TMain prevMainPtr;

        bool CanBeResponseFile(const TString& s) {
            return s.length() > 1 && s[0] == '@' && s[1] != '@';
        }

        bool IsEscaped(const TString& s) {
            return s.length() > 1 && s[0] == '@' && s[1] == '@';
        }

        bool CanBeHandler(const TString& s) {
            return !s.empty() && s[0] != '-' && !CanBeResponseFile(s);
        }

        TString GetHandlerName(const TVector<TString>& args) {
            for (auto& arg : args) {
                // Also check against response files but that's ok.
                // We'll unlikely ever have a handler that starts with @
                if (CanBeHandler(arg)) {
                    return arg;
                }
            }
            return "";
        }

        constexpr size_t MaxExpandedArgs = 100000;

        bool ExpandResponseFiles(TVector<TString>& result, const TVector<TString>& args, TString& handler) {
            bool halt{false};
            for (size_t i = 0; i < args.size(); ++i) {
                if (result.size() > MaxExpandedArgs) {
                    Cerr << "Too many arguments after expanding response files (limit: " << MaxExpandedArgs << ").\n";
                    exit(1);
                }
                auto arg{args[i]};
                if (!handler && CanBeHandler(arg)) {
                    // first arg that can be handler is a handler
                    handler = arg;
                    // stop expansion if `tool` or `run` handler is met
                    halt = handler == "tool" || handler == "run";
                }
                if (halt) {
                    for (size_t j = i; j < args.size(); ++j) {
                        result.emplace_back(args[j]);
                    }
                    return halt;
                } else if (CanBeResponseFile(arg)) {
                    TString filePath(arg.substr(1));
                    TFsPath path(filePath);

                    if (path.Exists()) {
                        TFileInput input(filePath);
                        TString line;
                        TVector<TString> content;
                        while (input.ReadLine(line)) {
                            if (!line.empty()) {
                                content.push_back(std::move(line));
                            }
                        }
                        halt = ExpandResponseFiles(result, content, handler);
                    } else {
                        Cerr << "Response file '" << filePath << "' doesn't exist.\nDocumentation on response files in ya: https://docs.yandex-team.ru/yatool/usage/options\n";
                        exit(1);
                    }
                } else if (IsEscaped(arg)) {
                    // Escaping: @@username -> @username
                    result.emplace_back(arg.substr(1));
                } else {
                    result.emplace_back(arg);
                }
            }
            return halt;
        }

        struct TExpandResult {
            TVector<TString> Args;      // argv[0] + expanded argv[1..]
            TString HandlerName;
        };

        // Platform-independent: works entirely with UTF-8 TString.
        // argv0 and rawArgs must already be decoded to UTF-8.
        // Inserts argv0 at the front of result Args.
        TExpandResult ExpandArgs(TString argv0, TVector<TString> rawArgs) {
            TExpandResult result;
            if (GetEnv("DISABLE_YA_RESPONSE_FILES")) {
                result.HandlerName = GetHandlerName(rawArgs);
                result.Args = std::move(rawArgs);
            } else {
                ExpandResponseFiles(result.Args, rawArgs, result.HandlerName);
                result.Args.reserve(rawArgs.size() + 1);
            }
            result.Args.insert(result.Args.begin(), std::move(argv0));
            return result;
        }

        // Build a char** view into expandedArgs.
        // The returned vector's pointers are valid as long as expandedArgs is alive.
        TVector<char*> BuildCharArgv(TVector<TString>& expandedArgs) {
            TVector<char*> argv;
            argv.reserve(expandedArgs.size());
            for (auto& arg : expandedArgs) {
                argv.push_back(const_cast<char*>(arg.data()));
            }
            return argv;
        }

        bool allowLogging(const IYaHandler* handlerPtr, const TVector<TStringBuf> args) {
            if (!handlerPtr->AllowLogging()) {
                return false;
            }

            // find YA_NO_LOGS
            auto noLogsEnv = GetEnv("YA_NO_LOGS");
            auto noLogsArg = false;

            // find --no-logs
            for (size_t i = 1; i < args.size(); i++) {
                auto& arg = args[i];
                if (arg.length() > 0 && arg.at(0) != '-') {
                    break;
                }
                if (arg == "--no-logs") {
                    noLogsArg = true;
                    break;
                }
            }

            return !noLogsEnv && !noLogsArg;
        }

        void InitLoggerRespectConfig(
            const TFsPath& miscRoot,
            const IYaHandler* handlerPtr,
            const TVector<TStringBuf>& args,
            ELogPriority priority,
            bool verbose) {
            if (allowLogging(handlerPtr, args)) {
                InitLogger(miscRoot, args, priority, verbose);
            } else {
                InitNullLogger();
            }
        }

        // Platform-independent: finds and runs the C++ handler if registered.
        // expandedArgs must be UTF-8 TStrings; TStringBuf in args points into them.
        void RunCppHandler(
            const TVector<TString>& expandedArgs,
            const TString& handlerName,
            int newPgid,
            bool verbose)
        {
            if (!handlerName) {
                return;
            }
            const auto factory = TSingletonClassFactory<IYaHandler>::Get();
            IYaHandler* handlerPtr = factory->GetObjectPtr(handlerName);
            if (!handlerPtr) {
                return;
            }
            TVector<TStringBuf> args;
            args.reserve(expandedArgs.size());
            for (const auto& arg : expandedArgs) {
                // TStringBuf points into TString, not into char** — safe on both platforms
                args.push_back(arg);
            }
            const IConfig& config = GetConfig();
            InitLoggerRespectConfig(config.MiscRoot(), handlerPtr, args, TLOG_DEBUG, verbose);
            if (newPgid != 0) {
                DEBUG_LOG << "Ya changed its pgid: " << newPgid << "\n";
            }
            DEBUG_LOG << "Start handler " << handlerName << "\n";
            // If handler has no fall back to python it just does exit() and doesn't return here.
            handlerPtr->Run(args);
            DEBUG_LOG << "Fallback to python\n";
        }

        bool DetectVerbose(const TVector<TString>& expandedArgs) {
            for (size_t i = 1; i < expandedArgs.size(); ++i) {
                if (CanBeHandler(expandedArgs[i])) {
                    // detect ya-bin's verbose mode, not handler's one
                    return false;
                }
                if (expandedArgs[i] == "-v" || expandedArgs[i] == "--verbose") {
                    return true;
                }
            }
            return false;
        }

#ifdef _win_
        int Entry(int argc, wchar_t** argv) {
            // On Windows argv is genuinely wchar_t** (UTF-16 LE).
            // Decode to UTF-8 TString before any processing.
            TVector<TString> rawArgs;
            rawArgs.reserve(argc > 1 ? argc - 1 : 0);
            for (int i = 1; i < argc; ++i) {
                rawArgs.emplace_back(WideToUTF8(argv[i], wcslen(argv[i])));
            }
            TString argv0 = argc > 0 ? WideToUTF8(argv[0], wcslen(argv[0])) : TString{};
#else
        int Entry(int argc, char** argv) {
            TVector<TString> rawArgs(argv + 1, argv + argc);
            TString argv0 = argc > 0 ? TString{argv[0]} : TString{};
#endif

            auto [expandedArgs, handlerName] = ExpandArgs(std::move(argv0), std::move(rawArgs));

            auto charArgv = BuildCharArgv(expandedArgs);
            int expandedArgc = static_cast<int>(charArgv.size());

            ::NYa::InitYt(expandedArgc, charArgv.data());
            int newPgid = SetOwnProcessGroupId(expandedArgc, charArgv.data());
            InitWatchdogFromEnv();

            bool verbose = DetectVerbose(expandedArgs);
            RunCppHandler(expandedArgs, handlerName, newPgid, verbose);

#ifdef _win_
            // Convert expandedArgs back to wchar_t** so Python's pymain receives the correct type.
            // TUtf16String stores char16_t (wchar16), which has the same bit layout as wchar_t on
            // Windows (both are 16-bit UTF-16 LE), but they are distinct C++ types.
            // reinterpret_cast<wchar_t*>(char16_t*) is valid here since the sizes and encoding match.
            // wExpandedArgs owns the data; expandedWArgv holds non-owning pointers into it.
            // Both must stay alive until prevMainPtr returns.
            TVector<TUtf16String> wExpandedArgs;
            wExpandedArgs.reserve(expandedArgs.size());
            for (const auto& arg : expandedArgs) {
                wExpandedArgs.emplace_back(UTF8ToWide(arg));
            }
            TVector<wchar_t*> expandedWArgv;
            expandedWArgv.reserve(wExpandedArgs.size());
            for (auto& warg : wExpandedArgs) {
                expandedWArgv.push_back(reinterpret_cast<wchar_t*>(warg.begin()));
            }
            return prevMainPtr(static_cast<int>(expandedWArgv.size()), expandedWArgv.data());
#else
            return prevMainPtr(expandedArgc, charArgv.data());
#endif
        }

        int InitEntry() {
            prevMainPtr = mainptr;
            mainptr = Entry;
            return 0;
        }

        int initEntry = InitEntry();
    } // namespace
} // namespace NYa
