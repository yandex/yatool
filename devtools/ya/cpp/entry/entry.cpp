#include <cstddef>
#include <devtools/libs/response_file/response_file.h>
#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/snowden/snowden.h>
#include <devtools/ya/cpp/lib/ya_handler.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <devtools/ya/cpp/lib/pgroup.h>
#include <devtools/ya/cpp/entry/watchdog.h>

#include <util/folder/path.h>
#include "util/generic/fwd.h"
#include <util/generic/hash.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/stream/output.h>
#include <util/system/env.h>
#include <utility>

#ifdef _win_
    #include <util/charset/wide.h> // WideToUTF8, UTF8ToWide
    #include <cwchar>              // wcslen
#endif

namespace NYa {
    __attribute__((weak)) void InitYt() {
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

        bool CanBeResponseFile(TStringBuf s) {
            return s.length() > 1 && s[0] == '@' && s[1] != '@';
        }

        bool CanBeHandler(TStringBuf s) {
            return !s.empty() && s[0] != '-' && !CanBeResponseFile(s);
        }

        TString GetHandlerName(TConstArrayRef<const char*> args) {
            for (size_t i = 1; i < args.size(); ++i) {
                const char* arg = args[i];
                // Also check against response files but that's ok.
                // We'll unlikely ever have a handler that starts with @
                if (CanBeHandler(arg)) {
                    return arg;
                }
            }
            return "";
        }

        struct TExpandResult {
            TVector<const char*> Args; // argv[0] + expanded argv[1..]
            TString HandlerName;
        };

        // argv includes argv[0] and must already be decoded to UTF-8.
        // The caller owns argv and storage for the lifetime of the returned pointers.
        TExpandResult ExpandArgs(TConstArrayRef<const char*> argv, TVector<TString>& storage) {
            TExpandResult result;
            if (GetEnv("DISABLE_YA_RESPONSE_FILES")) {
                result.HandlerName = GetHandlerName(argv);
                result.Args.assign(argv.begin(), argv.end());
            } else {
                try {
                    result.Args = NResponseFile::ExpandResponseFiles(argv, storage, [&](TStringBuf arg) {
                        if (!result.HandlerName && CanBeHandler(arg)) {
                            result.HandlerName = arg;
                            return arg == "tool" || arg == "run" || arg == "curl";
                        }
                        return false;
                    });
                } catch (const yexception& error) {
                    Cerr << error.what() << "\nDocumentation on response files in ya: https://docs.yandex-team.ru/yatool/usage/options\n";
                    exit(1);
                }
            }
            return result;
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
        // expandedArgs borrows UTF-8 strings kept alive by Entry.
        void RunCppHandler(
            const TVector<const char*>& expandedArgs,
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

        bool DetectVerbose(const TVector<const char*>& expandedArgs) {
            for (size_t i = 1; i < expandedArgs.size(); ++i) {
                if (CanBeHandler(expandedArgs[i])) {
                    // detect ya-bin's verbose mode, not handler's one
                    return false;
                }
                if (TStringBuf(expandedArgs[i]) == "-v" || TStringBuf(expandedArgs[i]) == "--verbose") {
                    return true;
                }
            }
            return false;
        }

#ifdef _win_
        int Entry(int argc, wchar_t** argv) {
            // On Windows argv is genuinely wchar_t** (UTF-16 LE).
            // Decode to UTF-8 TString before any processing.
            TVector<TString> decodedArgs;
            decodedArgs.reserve(argc);
            for (int i = 0; i < argc; ++i) {
                decodedArgs.emplace_back(WideToUTF8(argv[i], wcslen(argv[i])));
            }
            TVector<const char*> rawArgs;
            rawArgs.reserve(decodedArgs.size());
            for (const auto& arg : decodedArgs) {
                rawArgs.push_back(arg.c_str());
            }
#else
        int Entry(int argc, char** argv) {
            const TConstArrayRef<const char*> rawArgs{argv, static_cast<size_t>(argc)};
#endif

            TVector<TString> argumentStorage;
            auto [expandedArgs, handlerName] = ExpandArgs(rawArgs, argumentStorage);

            const int expandedArgc = static_cast<int>(expandedArgs.size());

            ::NYa::InitYt();
            int newPgid = SetOwnProcessGroupId(expandedArgc, const_cast<char**>(expandedArgs.data()));
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
            expandedWArgv.reserve(wExpandedArgs.size() + 1);
            for (auto& warg : wExpandedArgs) {
                expandedWArgv.push_back(reinterpret_cast<wchar_t*>(warg.begin()));
            }
            expandedWArgv.push_back(nullptr);
            return prevMainPtr(expandedArgc, expandedWArgv.data());
#else
            expandedArgs.push_back(nullptr);
            // The legacy Python entry takes char**, but does not modify the strings.
            return prevMainPtr(expandedArgc, const_cast<char**>(expandedArgs.data()));
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
