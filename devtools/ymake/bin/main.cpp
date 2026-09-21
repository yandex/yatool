#include <devtools/ymake/build_result.h>
#include <devtools/libs/response_file/response_file.h>
#include <devtools/ymake/run_main.h>

#include <util/generic/yexception.h>
#include <util/stream/output.h>

int main(int argc, char** argv) {
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
    return YMakeMain(expandedArgc, const_cast<char**>(expandedArguments.data()));
}
