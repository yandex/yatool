#pragma once

#include "ya_conf_json_types.h"
#include "jsonload.h"

namespace NYa::NJsonLoad {
    using namespace NYa::NYaConfJson;

    template<>
    inline void FromJson(TPlatformReplacements& replacements, const TWrappedJsonValue& wrappedJson) {
        for (const auto& [from, jsonTo] : wrappedJson.GetMap()) {
            TVector<TString> stringTo{};
            FromJson(stringTo, jsonTo);
            TVector<TCanonizedPlatform> to{};
            for (const TString& s : stringTo) {
                to.push_back(TCanonizedPlatform(s));
            }
            replacements.emplace(TCanonizedPlatform(from), std::move(to));
        }
    }

    template<>
    inline void FromJson(TFormulaBase& formula, const TWrappedJsonValue& wrappedJson) {
        FromJson(formula.PlatformReplacements, wrappedJson.GetItem("platform_replacements"));
    }

    template<>
    inline void FromJson(NYa::TLegacyPlatform& platform, const TWrappedJsonValue& wrappedJson) {
        FromJson(platform.Os, wrappedJson.GetItem("os"));
        FromJson(platform.Arch, wrappedJson.GetItem("arch"), {});
    }

    template<>
    inline void FromJson(TToolChainTool& tool, const TWrappedJsonValue& wrappedJson) {
        FromJson(tool.Bottle, wrappedJson.GetItem("bottle"));
        FromJson(tool.Executable, wrappedJson.GetItem("executable"));
    }

    template<>
    inline void FromJson(TToolChainPlatform& platform, const TWrappedJsonValue& wrappedJson) {
        FromJson(platform.Host, wrappedJson.GetItem("host"));
        FromJson(platform.Target, wrappedJson.GetItem("target"));
        FromJson(platform.Default, wrappedJson.GetItem("default"), false);
    }

    template<>
    inline void FromJson(TTool& tool, const TWrappedJsonValue& wrappedJson) {
        FromJson(tool.Description, wrappedJson.GetItem("description"), TString{"No description"});
        FromJson(tool.Visible, wrappedJson.GetItem("visible"), true);
    }

    template<>
    inline void FromJson(TBottle& bottle, const TWrappedJsonValue& wrappedJson) {
        bottle.Name = wrappedJson.GetKey();
        FromJson(bottle.Formula, wrappedJson.GetItem("formula"));
        if (wrappedJson.Has("executable")) {
            const auto& executable = wrappedJson.GetItem("executable");
            if (executable.GetType() == NJson::JSON_STRING) {
                TString exec = executable.GetString();
                bottle.Executable = TBottleExecutables::TValueType();
                (*bottle.Executable)[exec] = {exec};
            } else {
                FromJson(bottle.Executable, executable);
            }
        }
    }

    template<>
    inline void FromJson(TToolChain& toolChain, const TWrappedJsonValue& wrappedJson) {
        toolChain.Name = wrappedJson.GetKey();
        FromJson(toolChain.Tools, wrappedJson.GetItem("tools"));
        FromJson(toolChain.Platforms, wrappedJson.GetItem("platforms"));
        if (wrappedJson.Has("params")) {
            FromJson(toolChain.Params, wrappedJson.GetItem("params"));
        }
        FromJson(toolChain.Env, wrappedJson.GetItem("env"));
    }

    template<>
    inline void FromJson(TYaConf& conf, const TWrappedJsonValue& wrappedJson) {
        FromJson(conf.Tools, wrappedJson.GetItem("tools"));
        FromJson(conf.Bottles, wrappedJson.GetItem("bottles"));
        FromJson(conf.ToolChains, wrappedJson.GetItem("toolchain"));
    }
}
