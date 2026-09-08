#pragma once

#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/ya_conf_json_types.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NYa::NTool {
    struct TToolInvocation {
        TVector<TString> NameParts;
        TString ToolName;
        TVector<TString> ToolOptions;
        NYaConfJson::TYaConf Config;
    };

    TToolInvocation LoadToolInvocation(const IConfig& config, const TVector<TString>& args);
}
