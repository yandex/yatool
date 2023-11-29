#pragma once

#include <util/generic/vector.h>
#include <util/generic/string.h>

namespace NYa::NTool {
    struct TToolOptions {
        TString ProgramName;
        TStringBuf UnsupportedOption;
        bool PrintPath;
        bool PrintToolChainPath;
        bool PrintFastPathError;
        bool NoFallbackToPython;
        TString HostPlatform;
        TString ToolName;
        TVector<TString> ToolOptions;

        bool operator==(const TToolOptions&) const = default;
    };

    void ParseOptions(TToolOptions& options, const TVector<TStringBuf>& args);

    namespace NTest {
        TVector<TStringBuf> GetUnsupportedOptions();
    }
}
