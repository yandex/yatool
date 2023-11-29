#include "toolchain_sandbox_id.h"

#include <devtools/ya/cpp/lib/class_registry.h>
#include <devtools/ya/cpp/lib/jsonload.h>
#include <devtools/ya/cpp/lib/logger.h>
#include <devtools/ya/cpp/lib/ya_conf_json_loaders.h>

#include <util/string/join.h>

namespace NYa::NTool {
    struct TSandboxIdFormula : public NYaConfJson::TFormulaBase {
        TVector<long long> SandboxIds;
        TString Match;
    };

    struct TResourceInfo {
        struct TTask {
            long long Id;
        };

        long long Id;
        TTask Task;
        TString Description;
        THashMap<TString, NJson::TJsonValue> Attributes;
    };
    using TPlatformCache = THashMap<TString, TResourceInfo>;
}

namespace NYa::NJsonLoad {
    template<>
    inline void FromJson(NTool::TSandboxIdFormula& formula, const TWrappedJsonValue& wrappedJson) {
        FromJson<NYaConfJson::TFormulaBase>(formula, wrappedJson);
        TWrappedJsonValue sandboxId = wrappedJson.GetItem("sandbox_id");
        if (sandboxId.GetType() != NJson::JSON_ARRAY) {
            formula.SandboxIds = {sandboxId.GetIntegerRobust()};
        } else {
            FromJson(formula.SandboxIds, wrappedJson.GetItem("sandbox_id"));
        }
        FromJson(formula.Match, wrappedJson.GetItem("match"));
    }

    template <>
    void FromJson(NTool::TResourceInfo::TTask& task, const NJsonLoad::TWrappedJsonValue& jsonValue) {
        FromJson(task.Id, jsonValue.GetItem("id"));
    }

    template <>
    void FromJson(NTool::TResourceInfo& info, const NJsonLoad::TWrappedJsonValue& jsonValue) {
        FromJson(info.Id, jsonValue.GetItem("id"));
        FromJson(info.Task, jsonValue.GetItem("task"));
        FromJson(info.Description, jsonValue.GetItem("description"));
        if (jsonValue.Has("attributes")) {
            FromJson(info.Attributes, jsonValue.GetItem("attributes"));
        }
    }
}

namespace NYa::NTool {
    namespace NPrivate {
        bool MatchResourceInfo(const TSandboxIdFormula& formula, const TResourceInfo& info) {
            TString lowMatch = to_lower(formula.Match);
            TString lowDesc1 = to_lower(info.Description);
            TString lowDesc2;
            if (const NJson::TJsonValue* descPtr = info.Attributes.FindPtr("description")) {
                if (descPtr->IsString()) {
                    lowDesc2 = to_lower(descPtr->GetString());
                }
            }
            return FindPtr(formula.SandboxIds, info.Task.Id) && (lowDesc1.Contains(lowMatch) || lowDesc2.Contains(lowMatch));
        }
    }

    TFsPath TSandboxIdToolChainPathGetter::GetPath(const TFsPath& toolRoot, const NYaConfJson::TBottle& bottle, const NJson::TJsonValue& formulaJson, const TCanonizedPlatform& forPlatform) const {
        Y_UNUSED(bottle);
        if (!formulaJson.Has("sandbox_id")) {
            return {};
        }
        TSandboxIdFormula formula;
        NJsonLoad::LoadFromJsonValue(formula, formulaJson);

        // Get newest platform.cache. file
        THolder<TPlatformCache> platformCache{};
        TFsPath platformCachePath{};
        time_t mTime{};
        for (int sandboxId : formula.SandboxIds) {
            TFsPath curPlatformCachePath = toolRoot / ToString(sandboxId) / ".platform.cache";
            if (curPlatformCachePath.Exists()) {
                TFileStat fileStat{};
                curPlatformCachePath.Stat(fileStat);
                if (fileStat.MTime > mTime) {
                    THolder<TPlatformCache> curPlatformCache = MakeHolder<TPlatformCache>();
                    try {
                        NJsonLoad::LoadFromFile(*curPlatformCache, curPlatformCachePath);
                        platformCache.Swap(curPlatformCache);
                        mTime = fileStat.MTime;
                        platformCachePath = curPlatformCachePath;
                    } catch (const yexception& e) {
                        WARNING_LOG << "Parse of json file '" << curPlatformCachePath << "' failed with error: " << e.what() << "\n";
                    }
                }
            }
        }

        if (!platformCache) {
            throw yexception() << "No '.platform.cache' file is found";
        }

        TVector<TString> platforms{};
        for (const auto& [platform, resourceInfo] : *platformCache) {
            if (NPrivate::MatchResourceInfo(formula, resourceInfo)) {
                platforms.push_back(platform);
            }
        }
        TString foundPlatform = MatchPlatform(forPlatform, platforms, formula.PlatformReplacements.Get());
        if (!foundPlatform) {
            throw yexception() << "Platform '" << forPlatform.AsString()
                << "' not found in platform cache: '"  << platformCachePath
                << "'. Existing platforms: [" << JoinSeq(", ", platforms) << "]";
        } else {
            DEBUG_LOG << "Matched platform: " << foundPlatform << "\n";
        }

        long long sandboxId = (*platformCache)[foundPlatform].Task.Id;
        return toolRoot / ToString(sandboxId);
    }

    static NYa::TClassRegistrar<IToolChainPathGetter, TSandboxIdToolChainPathGetter> registration{};
}
