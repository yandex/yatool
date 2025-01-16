#pragma once

#include <devtools/libs/yaplatform/platform.h>
#include <devtools/libs/yaplatform/platform_map.h>
#include <devtools/ya/cpp/lib/edl/common/loaders.h>
#include <devtools/ya/cpp/lib/edl/common/members.h>

#include <library/cpp/json/writer/json_value.h>

#include <util/generic/maybe.h>
#include <util/folder/path.h>

namespace NYa::NYaConfJson {
    using TByPlatform = THashMap<TString, NYa::TResourceDesc>;
    struct TByPlatformFormula {
        bool operator==(const TByPlatformFormula&) const = default;

        Y_EDL_MEMBERS(
            ((TByPlatform) ByPlatform),
            ((TMaybe<TPlatformReplacements>) PlatformReplacements)
        )
    };

    using TLatestMatchedQueryAttributes = TMaybe<THashMap<TString, TString>>;
    struct TLatestMatchedQuery {
        bool operator==(const TLatestMatchedQuery&) const = default;

        Y_EDL_MEMBERS(
            ((TString) ResourceType),
            ((TString) Owner),
            ((TLatestMatchedQueryAttributes) Attributes)
        )
    };

    struct TLatestMatched {
        TLatestMatched() : UpdateInterval(86400)
        {
        }

        TLatestMatched(const TLatestMatched&) = default;
        TLatestMatched(TLatestMatched&&) = default;
        TLatestMatched& operator=(const TLatestMatched&) = default;
        TLatestMatched& operator=(TLatestMatched&&) = default;
        bool operator==(const TLatestMatched&) const = default;

        Y_EDL_MEMBERS(
            ((TLatestMatchedQuery) Query),
            ((long long) UpdateInterval),
            ((bool) IgnorePlatform),
            ((TMaybe<TPlatformReplacements>) PlatformReplacements)
        )
    };

    struct TLatestMatchedFormula {
        bool operator==(const TLatestMatchedFormula&) const = default;

        Y_EDL_MEMBERS(
            ((TLatestMatched) LatestMatched)
        )
    };

    using TFormula = std::variant<TString, TByPlatformFormula, TLatestMatchedFormula>;

    struct TToolChainTool {
        Y_EDL_MEMBERS(
            ((TMaybe<TString>) Bottle),
            ((TMaybe<TString>) Executable)
        )
    };

    struct TToolChainPlatform {
        Y_EDL_MEMBERS(
            ((NYa::TLegacyPlatform) Host),
            ((TMaybe<NYa::TLegacyPlatform>) Target),
            ((bool) Default)
        )
    };

    using TToolChainTools = THashMap<TString, TToolChainTool>;
    using TToolChainPlatforms = TVector<TToolChainPlatform>;
    using TToolChainParams = TMaybe<NJson::TJsonValue>;
    using TToolChainEnv = TMaybe<THashMap<TString, TVector<TString>>>;

    struct TTool {
        Y_EDL_MEMBERS(
            ((TString) Description),
            ((bool) Visible)
        )
    };

    using TBottleExecutable = TVector<TString>;
    using TBottleExecutableMap = THashMap<TString, TBottleExecutable>;
    using TBottleExecutables = TMaybe<std::variant<TString, TBottleExecutableMap>>;

    struct TBottle {
        Y_EDL_MEMBERS(
            ((TString) Name),
            ((TFormula) Formula),
            ((TBottleExecutables) Executable)
        )
    };

    struct TToolChain {
        Y_EDL_MEMBERS(
            ((TString) Name),
            ((TToolChainTools) Tools),
            ((TToolChainPlatforms) Platforms),
            ((TToolChainParams) Params),
            ((TToolChainEnv) Env)
        )
    };

    using TTools = THashMap<TString, TTool>;
    using TBottles = THashMap<TString, TBottle>;
    using TToolChains = THashMap<TString, TToolChain>;

    struct TYaConf {
        Y_EDL_MEMBERS(
            ((TTools) Tools),
            ((TBottles) Bottles),
            ((TToolChains) ToolChains, "toolchain")
        )

        Y_EDL_DEFAULT_MEMBER((NYa::NEdl::TBlackHole) Unknown)

        void Finish() {
            for (auto& [name, bottle] : Bottles) {
                bottle.Name = name;
            }
        }
    };
}
