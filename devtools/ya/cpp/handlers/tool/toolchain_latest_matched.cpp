#include "toolchain_latest_matched.h"

#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/jsonload.h>
#include <devtools/ya/cpp/lib/ya_conf_json_loaders.h>

#include <util/string/join.h>

namespace NYa::NTool {
    using TLatestMatchedQueryAttributes = TMaybe<THashMap<TString, TString>>;
    struct TLatestMatchedQuery {
        TString ResourceType;
        TString Owner;
        TLatestMatchedQueryAttributes Attributes;
    };

    struct TLatestMatched {
        TLatestMatchedQuery Query;
        long long UpdateInterval = 86400;
        bool IgnorePlatform = false;
    };

    struct TLatestMatchedFormula : public NYaConfJson::TFormulaBase {
        TLatestMatched LatestMatched;
    };

    struct TLatestMatchedInfo {
        long long UpdateTime;
        long long ResourceId;
    };
}

namespace NYa::NJsonLoad {
    template<>
    inline void FromJson(NTool::TLatestMatchedQuery& query, const TWrappedJsonValue& wrappedJson) {
        FromJson(query.ResourceType, wrappedJson.GetItem("resource_type"));
        FromJson(query.Owner, wrappedJson.GetItem("resource_type"), {});
        FromJson(query.Attributes, wrappedJson.GetItem("attributes"));
    }

    template<>
    inline void FromJson(NTool::TLatestMatched& formula, const TWrappedJsonValue& wrappedJson) {
        FromJson(formula.Query, wrappedJson.GetItem("query"));
        FromJson(formula.UpdateInterval, wrappedJson.GetItem("update_interval"), 86400ll);
        FromJson(formula.IgnorePlatform, wrappedJson.GetItem("ignore_platform"), false);
    }

    template <>
    void FromJson(NTool::TLatestMatchedInfo& info, const TWrappedJsonValue& jsonValue) {
        FromJson(info.UpdateTime, jsonValue.GetItem("update_time"));
        FromJson(info.ResourceId, jsonValue.GetItem("resource_id"));
    }

    template<>
    inline void FromJson(NTool::TLatestMatchedFormula& formula, const TWrappedJsonValue& wrappedJson) {
        FromJson<NYaConfJson::TFormulaBase>(formula, wrappedJson);
        FromJson(formula.LatestMatched, wrappedJson.GetItem("latest_matched"));
    }
}

namespace NYa::NTool {
    const TFsPath DISABLE_AUTO_UPDATE_FILE_NAME = ".disable.auto.update";

    TFsPath TLatestMatchedToolChainPathGetter::GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NJson::TJsonValue& formulaJson, const TCanonizedPlatform& forPlatform) const {
        if (!formulaJson.Has("latest_matched")) {
            return {};
        }
        TLatestMatchedFormula formula;
        NJsonLoad::LoadFromJsonValue(formula, formulaJson);

        TFsPath toolChainPath = toolRoot / Join('-', bottle.Name, forPlatform.AsString());
        TFsPath infoPath = toolChainPath.GetPath() + ".info";
        long long updateTimeThreshold = time(nullptr) - formula.LatestMatched.UpdateInterval;

        if (!infoPath.Exists()) {
            throw yexception() << "Info file doesn't exist: " << infoPath;
        }

        TLatestMatchedInfo info;
        try {
            NJsonLoad::LoadFromFile(info, infoPath);
        } catch (const yexception& e) {
            yexception() << "Can not read '" << infoPath << "': " << e.what();
        }

        TFsPath disableAutoUpdatePath = toolRoot / DISABLE_AUTO_UPDATE_FILE_NAME;
        if (info.UpdateTime < updateTimeThreshold && !disableAutoUpdatePath.Exists()) {
            throw yexception() << "It is time to update the tool";
        }

        return toolChainPath;
     }

    static NYa::TClassRegistrar<IToolChainPathGetter, TLatestMatchedToolChainPathGetter> registration{};
}
