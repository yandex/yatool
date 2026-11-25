#pragma once

#include <devtools/libs/response_file/response_file.h>
#include <devtools/ymake/build_result.h>

#include <util/generic/yexception.h>
#include <util/charset/wide.h>
#include <util/stream/output.h>

namespace NYMake {

    template <class TMain>
    int RunWithResponseFiles(int argc, char** argv, TMain runMain) {
        TVector<TString> argumentStorage;
        TVector<const char*> expandedArguments;
        try {
            expandedArguments = NResponseFile::ExpandResponseFiles(
                {argv, static_cast<size_t>(argc)}, argumentStorage);
        } catch (const yexception& error) {
            Cerr << "Command line initialization failed with error: " << error.what() << Endl;
            return BR_FATAL_ERROR;
        }
        const int expandedArgc = static_cast<int>(expandedArguments.size());
        expandedArguments.push_back(nullptr);
        // YMakeMain reorders argv pointers but does not modify the argument strings.
        return runMain(expandedArgc, const_cast<char**>(expandedArguments.data()));
    }

#ifdef _win_
    template <class TMain>
    int RunWithWideResponseFiles(int argc, const wchar_t* const* argv, TMain runMain) {
        TVector<TString> argumentStorage;
        argumentStorage.reserve(argc);
        for (int index = 0; index < argc; ++index) {
            argumentStorage.push_back(WideToUTF8(std::wstring_view{argv[index]}));
        }
        TVector<char*> arguments;
        arguments.reserve(argc + 1);
        for (auto& argument : argumentStorage) {
            arguments.push_back(argument.begin());
        }
        arguments.push_back(nullptr);
        return RunWithResponseFiles(argc, arguments.data(), runMain);
    }
#endif

} // namespace NYMake
