#pragma once

#include <devtools/libs/yaplatform/platform.h>
#include <devtools/ya/cpp/lib/ya_conf_json_types.h>
#include <devtools/ya/cpp/lib/config.h>
#include <devtools/ya/cpp/lib/process.h>

#include <util/folder/path.h>

namespace NYa::NTool {
    struct TTool {
        TString ToolName;
        TFsPath ToolChainPath;
        TFsPath ToolPath;
        NYaConfJson::TToolChainEnv Env;
    };

    class IToolChainPathGetter {
    public:
        virtual TFsPath GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NJson::TJsonValue& formulaJson, const TCanonizedPlatform& forPlatform) const = 0;
        virtual ~IToolChainPathGetter() = default;
    };

    TTool GetTool(const IConfig& config, const TString& toolName, const TCanonizedPlatform& forPlatform);
    void ExecTool(const IConfig& config, const TTool& tool, TVector<TString> toolOptions, TExecve execve = Execve);
}
