#pragma once

#include "toolchain.h"

#include <devtools/libs/yaplatform/platform.h>
#include <devtools/ya/cpp/lib/config.h>

#include <util/folder/path.h>

namespace NYa::NTool {
    class TSandboxIdToolChainPathGetter : public IToolChainPathGetter {
    public:
        TFsPath GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NJson::TJsonValue& formulaJson, const TCanonizedPlatform& forPlatform) const override;
    };
}
