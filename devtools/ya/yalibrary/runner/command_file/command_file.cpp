#include "command_file.h"

#include <util/stream/file.h>
#include <util/string/cast.h>

namespace {
    static const TString StartMarker = "--ya-start-command-file";
    static const TString EndMarker = "--ya-end-command-file";
} // namespace

namespace NCommandFile {
    TCommandArgsPacker::TCommandArgsPacker(const TString& buildRoot)
        : Counter(0)
        , BuildRoot(buildRoot)
    {}

    TVector<TString> TCommandArgsPacker::Pack(const TVector<TString>& commandArgs) {
        TVector<TString> args;
        size_t i = 0;
        args.reserve(commandArgs.size());
        while (i < commandArgs.size()) {
            if (commandArgs[i] == StartMarker) {
                TFsPath commandFilePath = BuildRoot / TString::Join("ya_command_file_", ToString(Counter), ".args");
                TFileOutput out(commandFilePath);
                i += 1;
                while (i < commandArgs.size()) {
                    if (commandArgs[i] == EndMarker) {
                        break;
                    }
                    out.Write(commandArgs[i]);
                    out.Write("\n");
                    i += 1;
                }
                args.push_back("@" + commandFilePath.GetPath());
                Counter += 1;
            } else {
                args.push_back(commandArgs[i]);
            }
            i += 1;
        }
        return args;
    }
} // namespace NCommandFile
