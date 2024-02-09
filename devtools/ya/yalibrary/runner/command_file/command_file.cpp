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
        for(size_t i = 0; i < commandArgs.size(); i++) {
            if (commandArgs[i] == StartMarker) {
                const auto& arg = ConsumeCommandFileArgs(commandArgs, ++i); // move i to skip marker
                args.push_back(arg);
            } else {
                args.push_back(commandArgs[i]);
            }
        }
        return args;
    }

    TString TCommandArgsPacker::ConsumeCommandFileArgs(const TVector<TString>& commandArgs, size_t& pos) {
        TFsPath commandFilePath = BuildRoot / TString::Join("ya_command_file_", ToString(Counter++), ".args");
        TFileOutput out(commandFilePath);

        for (; pos < commandArgs.size(); pos++) {
            if (commandArgs[pos] == StartMarker) {
               const auto arg = ConsumeCommandFileArgs(commandArgs, ++pos); // move pos to skip marker
               out.Write(arg);
            } else if (commandArgs[pos] == EndMarker) {
                break;
            } else {
                out.Write(commandArgs[pos]);
            }
            out.Write("\n");
        }

        return "@" + commandFilePath.GetPath();
    }
} // namespace NCommandFile
