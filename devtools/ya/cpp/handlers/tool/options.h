#pragma once

#include <util/generic/vector.h>
#include <util/generic/string.h>

namespace NYa::NTool {
    struct TToolOptions {
        TString ProgramName;
        bool PrintPath;
        bool PrintToolChainPath;
        bool PrintFastPathError;
        bool NoFallbackToPython;
        TString HostPlatform;
        TVector<TString> Args;
        bool SwallowDoubleDash;
        bool Dummy;

        bool operator==(const TToolOptions&) const = default;
    };

    void ParseOptions(TToolOptions& options, const TVector<TStringBuf>& args);

    namespace NTest {
        TVector<TStringBuf> GetLegacyOptions();
    }
}
