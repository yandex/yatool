#include "toolchain_by_platform.h"

#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/ya_conf_json_loaders.h>
#include <devtools/libs/yaplatform/platform_map.h>

#include <util/string/join.h>

namespace NYa::NTool {
    TFsPath TByPlatformToolChainPathGetter::GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NYaConfJson::TFormula& formula, const TCanonizedPlatform& forPlatform) const {
        Y_UNUSED(bottle);
        if (!std::holds_alternative<NYaConfJson::TByPlatformFormula>(formula)) {
            return {};
        }
        const NYaConfJson::TByPlatformFormula& formulaValue = std::get<NYaConfJson::TByPlatformFormula>(formula);

        TVector<TString> platforms;
        for (const auto& [platform, _] : formulaValue.ByPlatform) {
            platforms.push_back(platform);
        }

        TString foundPlatform = MatchPlatform(forPlatform, platforms, formulaValue.PlatformReplacements.Get());
        if (!foundPlatform) {
            throw yexception() << "Platform '" << forPlatform.AsString()
                << "' not found. Existing platforms: [" << JoinSeq(", ", platforms) << "]";
        }
        const TResourceDesc& resourceDesc = formulaValue.ByPlatform.at(foundPlatform);
        TString resourceDir = NYa::ResourceDirName(resourceDesc);
        return toolRoot / resourceDir;
    }

    static NYa::TClassRegistrar<IToolChainPathGetter, TByPlatformToolChainPathGetter> registration{};
}
