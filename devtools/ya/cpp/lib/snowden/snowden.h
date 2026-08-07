#pragma once

#include <devtools/ya/cpp/lib/config.h>

#include <util/generic/string.h>

namespace NYa::NSnowden {
    // No-op unless YA_SNOWDEN_MODE == "standalone", never throws.
    void EnsureDaemon(const IConfig& config);

    void ReportCppHandlerEvent(const TString& handlerName);

    void ReportToolExecutionEvent(
        const IConfig& config,
        const TString& toolName,
        const TString& toolPath
    );
} // namespace NYa::NSnowden
