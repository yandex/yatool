#include "snowden.h"
#include "snowden_private.h"

#include <devtools/ya/cpp/lib/logger.h>

#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <util/generic/yexception.h>
#include <util/stream/null.h>
#include <util/string/type.h>
#include <util/system/env.h>
#include <util/system/error.h>
#include <util/system/execpath.h>
#include <util/system/shellcommand.h>

#if defined(_unix_)
    #include <cerrno>
    #include <csignal>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
#elif defined(_win_)
    #include <algorithm>
    #include <cerrno>
    #include <memory>
    #include <process.h>
    #include <string>

    #include <util/charset/wide.h>
    #include <util/system/winint.h>
#endif

namespace NYa::NSnowden {
    bool ReportingDisabled(const TVector<TString>& expandedArgs) {
        const TString noReport = GetEnv("YA_NO_REPORT");
        if (IsTrue(noReport)) {
            return true;
        }
        for (size_t i = 1; i < expandedArgs.size(); ++i) {
            const auto& arg = expandedArgs[i];
            if (!arg.empty() && arg[0] != '-') {
                break;
            }
            if (arg == "--no-report") {
                return true;
            }
        }
        return false;
    }

    namespace {
        bool IsPythonEntryPointEnvironmentVariable(const std::wstring& variable) {
            constexpr wchar_t name[] = L"Y_PYTHON_ENTRY_POINT";
            constexpr size_t nameSize = sizeof(name) / sizeof(*name) - 1;
            if (variable.size() <= nameSize || variable[nameSize] != L'=') {
                return false;
            }

            for (size_t i = 0; i < nameSize; ++i) {
                wchar_t ch = variable[i];
                if (ch >= L'a' && ch <= L'z') {
                    ch -= L'a' - L'A';
                }
                if (ch != name[i]) {
                    return false;
                }
            }
            return true;
        }

#if defined(_unix_)
        void SpawnDetached(const TString& executable, const TList<TString>& args, const TString& entryPoint) {
            THashMap<TString, TString> env = NYa::Environ();
            env["Y_PYTHON_ENTRY_POINT"] = entryPoint;

            TVector<TString> argvStorage;
            argvStorage.reserve(args.size() + 1);
            argvStorage.push_back(executable);
            argvStorage.insert(argvStorage.end(), args.begin(), args.end());
            TVector<char*> argv;
            argv.reserve(argvStorage.size() + 1);
            for (auto& arg : argvStorage) {
                argv.push_back(arg.begin());
            }
            argv.push_back(nullptr);

            TVector<TString> envStorage;
            envStorage.reserve(env.size());
            for (const auto& [key, value] : env) {
                envStorage.push_back(key + "=" + value);
            }
            TVector<char*> envp;
            envp.reserve(envStorage.size() + 1);
            for (auto& var : envStorage) {
                envp.push_back(var.begin());
            }
            envp.push_back(nullptr);

            const int devNull = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (devNull < 0) {
                DEBUG_LOG << "[snowden] Cannot open /dev/null: " << LastSystemErrorText() << "\n";
                return;
            }

            // Same as TShellCommand: block signals so that no parent handler runs in the forked copy.
            sigset_t allSignals;
            sigset_t oldMask;
            sigfillset(&allSignals);
            pthread_sigmask(SIG_SETMASK, &allSignals, &oldMask);

            const pid_t intermediate = fork();
            if (intermediate == 0) {
                // The grandchild must not be a session leader, so it cannot acquire a controlling terminal.
                setsid();
                const pid_t grandchild = fork();
                if (grandchild == 0) {
                    // Same as TShellCommand: reset dispositions, SIG_IGN would survive execve().
                    struct sigaction sa = {};
                    sa.sa_handler = SIG_DFL;
                    sigemptyset(&sa.sa_mask);
                    for (int sig = 1; sig < NSIG; ++sig) {
                        sigaction(sig, &sa, nullptr);
                    }
                    pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);

                    // dup2() clears FD_CLOEXEC on the new descriptors.
                    dup2(devNull, STDIN_FILENO);
                    dup2(devNull, STDOUT_FILENO);
                    dup2(devNull, STDERR_FILENO);
                    execve(argv[0], argv.data(), envp.data());
                    _exit(127);
                }
                // The grandchild is reparented to init (or the nearest subreaper).
                _exit(grandchild < 0 ? 1 : 0);
            }
            const int forkErrno = errno;
            pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
            close(devNull);
            if (intermediate < 0) {
                DEBUG_LOG << "[snowden] Cannot fork child process: " << LastSystemErrorText(forkErrno) << "\n";
                return;
            }

            int status = 0;
            pid_t waited = 0;
            do {
                waited = waitpid(intermediate, &status, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited < 0 && errno != ECHILD) {
                DEBUG_LOG << "[snowden] Cannot wait for child process: " << LastSystemErrorText() << "\n";
            } else if (waited > 0 && !(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
                DEBUG_LOG << "[snowden] Cannot fork grandchild process, child status: " << status << "\n";
            }
        }
#elif defined(_win_)
        std::wstring ToWide(TStringBuf value) {
            const TUtf16String utf16 = UTF8ToWide(value);
            return std::wstring(utf16.begin(), utf16.end());
        }

        std::wstring GetCurrentExecutablePathWide() {
            TVector<wchar_t> buffer(MAX_PATH);
            for (;;) {
                const DWORD size = GetModuleFileNameW(
                    nullptr,
                    buffer.data(),
                    static_cast<DWORD>(buffer.size())
                );
                if (size == 0) {
                    ythrow TSystemError() << "Cannot get current executable path";
                }
                if (size < buffer.size()) {
                    return std::wstring(buffer.data(), size);
                }
                buffer.resize(buffer.size() * 2);
            }
        }

        std::wstring GetWindowsExecutablePath(TStringBuf executable) {
            // GetExecPath() uses the narrow Windows API. Avoid interpreting its
            // active-code-page bytes as UTF-8 when relaunching the current ya-bin.
            return executable == GetExecPath() ? GetCurrentExecutablePathWide() : ToWide(executable);
        }

        std::wstring ToSpawnArgument(TStringBuf value) {
            return NPrivate::QuoteWindowsArgument(ToWide(value));
        }

        void SpawnDetached(const TString& executable, const TList<TString>& args, const TString& entryPoint) {
            const std::wstring wideExecutable = GetWindowsExecutablePath(executable);

            TVector<std::wstring> argvStorage;
            argvStorage.reserve(args.size() + 1);
            argvStorage.push_back(NPrivate::QuoteWindowsArgument(wideExecutable));
            for (const auto& arg : args) {
                argvStorage.push_back(ToSpawnArgument(arg));
            }

            TVector<const wchar_t*> argv;
            argv.reserve(argvStorage.size() + 1);
            for (const auto& arg : argvStorage) {
                argv.push_back(arg.c_str());
            }
            argv.push_back(nullptr);

            // NYa::Environ() contains Windows MBCS strings. Read the native Unicode block
            // directly so non-ASCII paths are not interpreted as UTF-8.
            wchar_t* rawEnvironment = GetEnvironmentStringsW();
            if (!rawEnvironment) {
                ythrow TSystemError() << "Cannot read Windows environment";
            }
            const std::unique_ptr<wchar_t, decltype(&FreeEnvironmentStringsW)> environment{
                rawEnvironment,
                &FreeEnvironmentStringsW,
            };
            TVector<std::wstring> envStorage = NPrivate::BuildWindowsEnvironment(
                environment.get(),
                ToWide(entryPoint)
            );
            std::sort(envStorage.begin(), envStorage.end(), [](const std::wstring& lhs, const std::wstring& rhs) {
                return CompareStringOrdinal(
                    lhs.data(),
                    static_cast<int>(lhs.size()),
                    rhs.data(),
                    static_cast<int>(rhs.size()),
                    TRUE
                ) == CSTR_LESS_THAN;
            });

            TVector<const wchar_t*> envp;
            envp.reserve(envStorage.size() + 1);
            for (const auto& var : envStorage) {
                envp.push_back(var.c_str());
            }
            envp.push_back(nullptr);

            errno = 0;
            if (_wspawnve(_P_DETACH, wideExecutable.c_str(), argv.data(), envp.data()) == -1) {
                ythrow TSystemError(errno) << "Cannot start detached Snowden process";
            }
        }
#endif
    } // namespace

    namespace NPrivate {
        EPythonEntryPointLaunchMode SelectPythonEntryPointLaunchMode(bool async) {
            return async ? EPythonEntryPointLaunchMode::Detached : EPythonEntryPointLaunchMode::Synchronous;
        }

        std::wstring QuoteWindowsArgument(const std::wstring& argument) {
            std::wstring result;
            result.reserve(argument.size() + 2);
            result.push_back(L'"');

            size_t backslashes = 0;
            for (const wchar_t character : argument) {
                if (character == L'\\') {
                    ++backslashes;
                    continue;
                }

                result.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
                result.push_back(character);
                backslashes = 0;
            }

            result.append(backslashes * 2, L'\\');
            result.push_back(L'"');
            return result;
        }

        TVector<std::wstring> BuildWindowsEnvironment(
            const wchar_t* environment,
            const std::wstring& entryPoint
        ) {
            Y_ENSURE(environment, "Windows environment must not be null");

            TVector<std::wstring> result;
            for (const wchar_t* variable = environment; *variable;) {
                std::wstring value{variable};
                variable += value.size() + 1;
                if (!IsPythonEntryPointEnvironmentVariable(value)) {
                    result.push_back(std::move(value));
                }
            }
            result.push_back(L"Y_PYTHON_ENTRY_POINT=" + entryPoint);
            return result;
        }

        TShellCommandOptions BuildPythonEntryPointOptions(bool async) {
            TShellCommandOptions opts;
            opts
                .SetDetachSession(true)
                .SetAsync(async)
                .SetUseShell(false)
                .SetQuoteArguments(true)
                .SetOutputStream(nullptr)
                .SetErrorStream(nullptr);
            return opts;
        }

        TMaybe<int> RunPythonEntryPoint(
            const TString& executable,
            const TString& entryPoint,
            const TList<TString>& args,
            bool async
        ) {
            if (SelectPythonEntryPointLaunchMode(async) == EPythonEntryPointLaunchMode::Detached) {
                SpawnDetached(executable, args, entryPoint);
                return Nothing();
            }

            THashMap<TString, TString> env = NYa::Environ();
            env["Y_PYTHON_ENTRY_POINT"] = entryPoint;

#if defined(_unix_)
            // Keep fd 0 occupied while TShellCommand creates its output pipes.
            // Otherwise an output pipe can take fd 0 and close the redirected
            // stdin in the child while rearranging descriptors after fork().
            TFileHandle stdinReservation("/dev/null", OpenExisting | RdOnly | CloseOnExec);
            Y_ENSURE(stdinReservation.IsOpen(), "Cannot reserve stdin for Snowden child process");
#endif
            TShellCommandOptions opts = BuildPythonEntryPointOptions(async);
            TNullInput nullIn;
            opts.SetInputStream(&nullIn);

            opts.Environment = std::move(env);

            TShellCommand cmd(executable, args, opts);
            cmd.Run();
            return cmd.GetExitCode();
        }

        TList<TString> BuildToolHandlerEventArguments(
            const TVector<TString>& expandedArgs,
            const TVector<TString>& toolNameParts,
            const TVector<TString>& toolArgs
        ) {
            NJson::TJsonValue prefix{NJson::JSON_ARRAY};
            prefix.AppendValue("ya");
            prefix.AppendValue("tool");

            NJson::TJsonValue args{NJson::JSON_ARRAY};
            for (const auto& arg : ExtractHandlerArguments(expandedArgs, "tool")) {
                args.AppendValue(arg);
            }

            NJson::TJsonValue toolName{NJson::JSON_ARRAY};
            for (const auto& part : toolNameParts) {
                toolName.AppendValue(part);
            }

            NJson::TJsonValue handlerToolArgs{NJson::JSON_ARRAY};
            for (const auto& arg : toolArgs) {
                handlerToolArgs.AppendValue(arg);
            }

            NJson::TJsonValue value{NJson::JSON_MAP};
            value["prefix"] = std::move(prefix);
            value["args"] = std::move(args);
            value["handler_source"] = "cpp_dispatch";
            value["tool_name"] = std::move(toolName);
            value["tool_args"] = std::move(handlerToolArgs);
            return {
                "--key=handler",
                TString("--value-json=") + NJson::WriteJson(value, false),
            };
        }

        TList<TString> BuildToolExecutionEventArguments(
            const TString& toolName,
            const TString& toolPath,
            const TVector<TString>& toolArgs
        ) {
            NJson::TJsonValue args{NJson::JSON_ARRAY};
            for (const auto& arg : toolArgs) {
                args.AppendValue(arg);
            }

            NJson::TJsonValue value{NJson::JSON_MAP};
            value["tool_launch_method"] = "cpp_fast_path";
            value["tool_name"] = toolName;
            value["tool_path"] = toolPath;
            value["extra_args"] = std::move(args);
            return {
                "--key=tool_execution",
                TString("--value-json=") + NJson::WriteJson(value, false),
            };
        }
    }

    namespace {

        void SpawnPythonEntryPoint(const TString& entryPoint, const TList<TString>& args) {
            NPrivate::RunPythonEntryPoint(GetExecPath(), entryPoint, args, true);
        }

    } // namespace

    void EnsureDaemon(const IConfig& /*config*/) {
        try {
            if (ReportingDisabled({}) || GetEnv("YA_SNOWDEN_MODE") != "standalone") {
                return;
            }
            SpawnPythonEntryPoint(
                "yalibrary.snowden:ensure_daemon_main",
                {}
            );
            DEBUG_LOG << "[snowden] EnsureDaemon initiated\n";
        } catch (...) {
            DEBUG_LOG << "[snowden] EnsureDaemon failed silently\n";
        }
    }

    TVector<TString> ExtractHandlerArguments(
        const TVector<TString>& expandedArgs,
        const TString& handlerName
    ) {
        for (size_t i = 1; i < expandedArgs.size(); ++i) {
            if (expandedArgs[i] == handlerName) {
                return TVector<TString>(expandedArgs.begin() + i + 1, expandedArgs.end());
            }
        }
        return expandedArgs;
    }

    void ReportToolHandlerEvent(
        const TVector<TString>& expandedArgs,
        const TVector<TString>& toolNameParts,
        const TVector<TString>& toolArgs
    ) {
        try {
            SpawnPythonEntryPoint(
                "yalibrary.snowden:push_event_main",
                NPrivate::BuildToolHandlerEventArguments(expandedArgs, toolNameParts, toolArgs)
            );
            DEBUG_LOG << "[snowden] Handler event push initiated: tool\n";
        } catch (...) {
            DEBUG_LOG << "[snowden] ReportToolHandlerEvent failed silently: " << CurrentExceptionMessage() << "\n";
        }
    }

    void ReportToolExecutionEvent(
        const IConfig& /*config*/,
        const TString& toolName,
        const TString& toolPath,
        const TVector<TString>& toolArgs
    ) {
        try {
            SpawnPythonEntryPoint(
                "yalibrary.snowden:push_event_main",
                NPrivate::BuildToolExecutionEventArguments(toolName, toolPath, toolArgs)
            );
            DEBUG_LOG << "[snowden] ToolExecution event push initiated: " << toolName << "\n";
        } catch (...) {
            // Telemetry must never break tool execution.
            DEBUG_LOG << "[snowden] ReportToolExecutionEvent failed silently\n";
        }
    }

} // namespace NYa::NSnowden
