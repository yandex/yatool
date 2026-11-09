#include "command_file.h"

#include <util/stream/file.h>
#include <util/string/cast.h>

#include <stdexcept>

namespace {
    static const TString StartMarker = "--ya-start-command-file";
    static const TString EndMarker = "--ya-end-command-file";
} // namespace

namespace NCommandFile {
    TCommandArgsPacker::TCommandArgsPacker(const TString& buildRoot)
        : Counter(0)
        , BuildRoot(buildRoot)
    {
    }

    TVector<TString> TCommandArgsPacker::Pack(const TVector<TString>& commandArgs) {
        return Pack(commandArgs, {}).Arguments;
    }

    TPackedCommand TCommandArgsPacker::Pack(const TVector<TString>& commandArgs, const TPackOptions& options) {
        if (options.DiagnosticArguments && options.DiagnosticArguments->size() != commandArgs.size()) {
            throw std::invalid_argument("diagnostic arguments must match execution arguments");
        }
        TPackedCommand result;
        if (options.DiagnosticArguments) {
            result.DiagnosticArguments.emplace();
        }
        for (size_t i = 0; i < commandArgs.size(); i++) {
            if (commandArgs[i] == StartMarker) {
                const auto arg = ConsumeCommandFileArgs(commandArgs, ++i);
                result.Arguments.push_back(arg);
                if (result.DiagnosticArguments) {
                    // Verbose output must name the file actually passed to the
                    // process, without creating a second file of redacted data.
                    result.DiagnosticArguments->push_back(arg);
                }
            } else {
                result.Arguments.push_back(commandArgs[i]);
                if (result.DiagnosticArguments) {
                    result.DiagnosticArguments->push_back((*options.DiagnosticArguments)[i]);
                }
            }
        }
        return result;
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
