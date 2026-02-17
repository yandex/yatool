#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/ya_handler.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <devtools/ya/cpp/lib/pgroup.h>
#include <devtools/ya/cpp/entry/watchdog.h>

#include <util/folder/path.h>
#include "util/generic/fwd.h"
#include <util/generic/hash.h>
#include <util/stream/file.h>
#include <util/system/env.h>

namespace NYa {
    __attribute__((weak)) void InitYt(int, char**) {
    }

    namespace {
        using TMain = int (*)(int argc, char** argv);

        // Pointer to main function used by arcadia python:
        // Python2: https://a.yandex-team.ru/arcadia/library/python/runtime/main/main.c?rev=f778e4e75ab5665f92abd1466726f4e5d51d4029#L42
        // Python3: https://a.yandex-team.ru/arcadia/library/python/runtime_py3/main/main.c?rev=f778e4e75ab5665f92abd1466726f4e5d51d4029#L231
        extern "C" TMain mainptr;
        TMain prevMainPtr;

        TVector<TString> ExpandResponseFiles(int argc, char** argv) {
            TVector<TString> result;
            result.reserve(argc);

            for (int i = 0; i < argc; ++i) {
                TStringBuf arg(argv[i]);
                if (arg.length() > 1 && arg[0] == '@' && arg[1] != '@') {
                    TString filePath(arg.substr(1));
                    TFsPath path(filePath);

                    if (path.Exists()) {
                        TFileInput input(filePath);
                        TString line;
                        while (input.ReadLine(line)) {
                            if (!line.empty()) {
                                result.push_back(std::move(line));
                            }
                        }
                    } else {
                        Cerr << "Response file '" << filePath << "' doesn't exist.\n";
                        exit(1);
                    }
                } else if (arg.length() > 1 && arg.substr(0, 2) == "@@") {
                    // Escaping: @@username -> @username
                    result.emplace_back(arg.substr(1));
                } else {
                    result.emplace_back(arg);
                }
            }

            return result;
        }

        bool allowLogging(const IYaHandler *handlerPtr, const TVector<TStringBuf> args) {
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
            const TFsPath &miscRoot,
            const IYaHandler *handlerPtr,
            const TVector<TStringBuf> &args,
            ELogPriority priority,
            bool verbose
        ) {
            if (allowLogging(handlerPtr, args)) {
                InitLogger(miscRoot, args, priority, verbose);
            } else {
                InitNullLogger();
            }
        }

        int Entry(int argc, char** argv) {
            TVector<TString> expandedArgs = ExpandResponseFiles(argc, argv);
            TVector<char*> expandedArgv;
            expandedArgv.reserve(expandedArgs.size());
            for (auto& arg : expandedArgs) {
                expandedArgv.push_back(const_cast<char*>(arg.data()));
            }
            int expandedArgc = static_cast<int>(expandedArgv.size());
            char** expandedArgvPtr = expandedArgv.data();

            ::NYa::InitYt(expandedArgc, expandedArgvPtr);
            auto newPgid = SetOwnProcessGroupId(expandedArgc, expandedArgvPtr);
            InitWatchdogFromEnv();

            const auto factory = TSingletonClassFactory<IYaHandler>::Get();
            // Find tool name (first non-flag arg)
            const char* handlerName = nullptr;
            bool verbose = false;
            for (int i = 1; i < expandedArgc; ++i) {
                if (expandedArgs[i] == "-v" || expandedArgs[i] == "--verbose") {
                    verbose = true;
                }
                if (expandedArgvPtr[i][0] != '-') {
                    handlerName = expandedArgvPtr[i];
                    break;
                }
            }
            if (handlerName) {
                if (IYaHandler* handlerPtr = factory->GetObjectPtr(handlerName)) {
                    TVector<TStringBuf> args;
                    args.reserve(expandedArgc);
                    for (int i = 0; i < expandedArgc; ++i) {
                        args.push_back(expandedArgvPtr[i]);
                    }
                    const IConfig& config = GetConfig();
                    InitLoggerRespectConfig(config.MiscRoot(), handlerPtr, args, TLOG_DEBUG, verbose);

                    if (newPgid != 0) {
                        DEBUG_LOG << "Ya changed its pgid: " << newPgid << "\n";
                    }

                    DEBUG_LOG << "Start handler " << handlerName << "\n";
                    // If handler has no fall back to python it just do exit() and doesn't return here.
                    handlerPtr->Run(args);
                    DEBUG_LOG << "Fallback to python\n";
                }
            }
            return prevMainPtr(expandedArgc, expandedArgvPtr);
        }

        int InitEntry() {
            prevMainPtr = mainptr;
            mainptr = Entry;
            return 0;
        }

        int initEntry = InitEntry();
    }
}
