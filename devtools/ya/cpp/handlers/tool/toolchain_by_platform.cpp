#include "toolchain_by_platform.h"

#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/jsonload.h>
#include <devtools/ya/cpp/lib/ya_conf_json_loaders.h>
#include <devtools/libs/yaplatform/platform_map.h>

#include <util/string/join.h>

namespace NYa::NTool {
    using TByPlatform = THashMap<TString, NYa::TResourceDesc>;
    struct TByPlatformFormula : public NYaConfJson::TFormulaBase {
         TByPlatform ByPlatform;
    };
}

namespace NYa::NJsonLoad {
    template<>
    inline void FromJson(NTool::TByPlatformFormula& formula, const TWrappedJsonValue& wrappedJson) {
        FromJson<NYaConfJson::TFormulaBase>(formula, wrappedJson);
        FromJson(formula.ByPlatform, wrappedJson.GetItem("by_platform"));
    }

    template<>
    inline void FromJson(NYa::TResourceDesc& desc, const TWrappedJsonValue& wrappedJson) {
        FromJson(desc.Uri, wrappedJson.GetItem("uri"));
        if (wrappedJson.Has("strip_prefix")) {
            long long tmp;
            FromJson(tmp, wrappedJson.GetItem("strip_prefix"));
            if (tmp < 0 || tmp > Max<ui32>()) {
                ythrow yexception() << "strip_prefix " << tmp << " is out of range";
            }
            desc.StripPrefix = tmp;
        }
    }
}

namespace NYa::NTool {
    TFsPath TByPlatformToolChainPathGetter::GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NJson::TJsonValue& formulaJson, const TCanonizedPlatform& forPlatform) const {
        Y_UNUSED(bottle);
        if (!formulaJson.Has("by_platform")) {
            return {};
        }
        TByPlatformFormula formula;
        NJsonLoad::LoadFromJsonValue(formula, formulaJson);

        TVector<TString> platforms;
        for (const auto& [platform, _] : formula.ByPlatform) {
            platforms.push_back(platform);
        }

        TString foundPlatform = MatchPlatform(forPlatform, platforms, formula.PlatformReplacements.Get());
        if (!foundPlatform) {
            throw yexception() << "Platform '" << forPlatform.AsString()
                << "' not found. Existing platforms: [" << JoinSeq(", ", platforms) << "]";
        }
        const TResourceDesc& resourceDesc = formula.ByPlatform.at(foundPlatform);
        TString resourceDir = NYa::ResourceDirName(resourceDesc);
        return toolRoot / resourceDir;
    }

    static NYa::TClassRegistrar<IToolChainPathGetter, TByPlatformToolChainPathGetter> registration{};
}
