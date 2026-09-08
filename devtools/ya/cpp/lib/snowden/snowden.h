#pragma once

#include <devtools/ya/cpp/lib/config.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYa::NSnowden {
    bool ReportingDisabled(const TVector<TString>& expandedArgs);

    // No-op unless YA_SNOWDEN_MODE == "standalone", never throws.
    void EnsureDaemon(const IConfig& config);

    TVector<TString> ExtractHandlerArguments(
        const TVector<TString>& expandedArgs,
        const TString& handlerName
    );

    void ReportToolHandlerEvent(
        const TVector<TString>& expandedArgs,
        const TVector<TString>& toolNameParts,
        const TVector<TString>& toolArgs
    );

    void ReportToolExecutionEvent(
        const IConfig& config,
        const TString& toolName,
        const TString& toolPath,
        const TVector<TString>& toolArgs
    );
} // namespace NYa::NSnowden
