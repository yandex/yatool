#include "logger.h"
#include "process.h"

#include <util/system/error.h>
#include <util/string/join.h>

#if defined(_win_)
    #undef execve
    #define execve _execve
#else
    #include <unistd.h>
#endif
#include <filesystem>

namespace NYa {
    void Execve(TFsPath bin, TVector<TString> args, const THashMap<TString, TString>& env, const TFsPath& cwd) {
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
        TVector<char *> envp;
        for (const auto & [key, value] : env) {
            envHolder.push_back(Join("=", key, value));
            envp.push_back(const_cast<char*>(envHolder.back().data()));
        }
        envp.push_back(nullptr);

        execve(argv[0], argv.data(), envp.data());
        throw yexception() << "execve() filed with error: " << LastSystemErrorText();
    }
}