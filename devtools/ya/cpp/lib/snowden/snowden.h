#pragma once

#include <devtools/ya/cpp/lib/config.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYa::NSnowden {
    // No-op unless YA_SNOWDEN_MODE == "standalone", never throws.
    void EnsureDaemon(const IConfig& config);

    TVector<TString> ExtractHandlerArguments(
        const TVector<TString>& expandedArgs,
        const TString& handlerName
    );

    void ReportCppHandlerEvent(
        const TString& handlerName,
        const TVector<TString>& expandedArgs
    );

    void ReportToolExecutionEvent(
        const IConfig& config,
        const TString& toolName,
        const TString& toolPath
    );
} // namespace NYa::NSnowden
