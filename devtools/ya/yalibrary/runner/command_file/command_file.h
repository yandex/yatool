#pragma once

#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <optional>

namespace NCommandFile {
    struct TPackOptions {
        const TVector<TString>* DiagnosticArguments = nullptr;
    };

    struct TPackedCommand {
        TVector<TString> Arguments;
        std::optional<TVector<TString>> DiagnosticArguments;
    };

    class TCommandArgsPacker {
    public:
        TCommandArgsPacker() {
        }
        explicit TCommandArgsPacker(const TString& buildRoot);
        TVector<TString> Pack(const TVector<TString>& commandArgs);
        TPackedCommand Pack(const TVector<TString>& commandArgs, const TPackOptions& options);

    private:
        TString ConsumeCommandFileArgs(const TVector<TString>& commandArgs, size_t& pos);

        size_t Counter;
        TFsPath BuildRoot;
    };
} // namespace NCommandFile
