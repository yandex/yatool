#pragma once

#include <devtools/libs/yaplatform/platform.h>

#include "library/cpp/json/json_value.h"

#include <util/generic/maybe.h>
#include <util/folder/path.h>

#include <variant>

namespace NYa::NYaConfJson {
    struct TFormulaBase {
        TMaybe<TPlatformReplacements> PlatformReplacements;
    };

    using TBottleExecutable = TVector<TString>;

    struct TToolChainTool {
        TMaybe<TString> Bottle;
        TMaybe<TString> Executable;
    };

    struct TToolChainPlatform {
        NYa::TLegacyPlatform Host;
        TMaybe<NYa::TLegacyPlatform> Target;
        bool Default = false;
    };

    using TBottleExecutables = TMaybe<THashMap<TString, TBottleExecutable>>;

    using TToolChainTools = THashMap<TString, TToolChainTool>;
    using TToolChainPlatforms = TVector<TToolChainPlatform>;
    using TToolChainParams = TMaybe<NJson::TJsonValue>;
    using TToolChainEnv = TMaybe<THashMap<TString, TVector<TString>>>;

    struct TTool {
        TString Description;
        bool Visible;
    };

    struct TBottle {
        TString Name;
        NJson::TJsonValue Formula;
        TBottleExecutables Executable;
    };

    struct TToolChain {
        TString Name;
        TToolChainTools Tools;
        TToolChainPlatforms Platforms;
        TToolChainParams Params;
        TToolChainEnv Env;
    };

    using TTools = THashMap<TString, TTool>;
    using TBottles = THashMap<TString, TBottle>;
    using TToolChains = THashMap<TString, TToolChain>;

    struct TYaConf {
        TTools Tools;
        TBottles Bottles;
        TToolChains ToolChains;
    };
}
