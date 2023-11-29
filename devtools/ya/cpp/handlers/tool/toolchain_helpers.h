#pragma once

// This header is for internal usage only.

#include "toolchain.h"

#include <devtools/libs/yaplatform/platform.h>
#include <devtools/ya/cpp/lib/ya_conf_json_types.h>
#include <devtools/ya/cpp/lib/config.h>

#include <util/folder/path.h>

namespace NYa::NTool::NPrivate {
    const NYaConfJson::TToolChain& ResolveTool(const NYaConfJson::TYaConf& config, TString toolName);

    TFsPath GetToolChainPath(
        const IConfig& config,
        const TFsPath& toolRoot,
        const TVector<IToolChainPathGetter*>& toolChainPathGetters,
        const NYaConfJson::TBottle& bottle,
        const TCanonizedPlatform& forPlatform
    );

    TFsPath GetToolPath(const TFsPath& toolChainPath, const TString& toolName, const NYaConfJson::TBottle& bottle, const NYaConfJson::TToolChainTool& toolChainTool);
}
