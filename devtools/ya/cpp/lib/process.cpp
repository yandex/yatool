#include "logger.h"
#include "process.h"

#include <util/system/error.h>
#include <util/string/join.h>

#if defined(_win_)
    #include <util/system/shellcommand.h>
#else
    #include <unistd.h>
    #include <filesystem>
#endif

namespace NYa {
    void Execve(const TFsPath& bin, const TVector<TString>& args, const THashMap<TString, TString>& env, const TFsPath& cwd) {
#ifdef _win_
        // Windows _exec is broken: https://stackoverflow.com/a/44551025/1838079
        // Use explicit process starting
        auto opts = TShellCommandOptions()
            .SetUseShell(false)
            .SetQuoteArguments(true);
        opts.Environment = env;

        TShellCommand cmd(bin.GetPath(), TList<TString>{args.begin(), args.end()}, opts, cwd);
        cmd.Run();
        if (cmd.GetStatus() == TShellCommand::SHELL_ERROR) {
            ythrow yexception() << "Cannot run program " << bin << ": " << cmd.GetError();
        }
        exit(cmd.GetExitCode().GetOrElse(1));
#else
        if (cwd) {
            DEBUG_LOG << "chdir to " << cwd << "\n";
            std::filesystem::current_path(cwd.GetPath().c_str());
        }
        // Fill argv
        TVector<char *> argv;
        argv.push_back(const_cast<char *>(bin.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Fill envp
        TVector<TString> envHolder;
        envHolder.reserve(env.size());  // Important. envHolder should not reallocate on push_back
        TVector<char *> envp;
        for (const auto & [key, value] : env) {
            envHolder.push_back(Join("=", key, value));
            envp.push_back(const_cast<char*>(envHolder.back().data()));
        }
        envp.push_back(nullptr);

        execve(argv[0], argv.data(), envp.data());
        throw yexception() << "execve() filed with error: " << LastSystemErrorText();
#endif
    }
}
