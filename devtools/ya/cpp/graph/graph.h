#pragma once

#include "intern_string.h"
#include "tinymap.h"

#include "devtools/ya/cpp/lib/edl/common/export_helpers.h"
#include "devtools/ya/cpp/lib/edl/common/members.h"

#include <library/cpp/json/writer/json_value.h>

#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <variant>

namespace NYa::NGraph {
    using TGraphString = TInternString;
    using TUid = TGraphString;
    using TFilePath = TGraphString;
    using TOutputs = TVector<TFilePath>;
    using TEnv = TTinyMap<TGraphString, TGraphString>;

    struct TGlobalSingleResource {
        Y_EDL_MEMBERS(
            ((TGraphString) Resource),
            ((TGraphString) Name),
            ((ui32) StripPrefix)
        )
    };

    struct TGlobalResourceBundleItem {
        Y_EDL_MEMBERS(
            ((TGraphString) Platform),
            ((TGraphString) Resource),
            ((ui32) StripPrefix)
        )
    };

    struct TGlobalResourceBundle {
        Y_EDL_MEMBERS(
            ((TVector<TGlobalResourceBundleItem>) Resources)
        )
    };

    using TGlobalResourceInfo = std::variant<TGlobalSingleResource, TGlobalResourceBundle>;

    struct TGlobalResource {
        Y_EDL_MEMBERS(
            ((TGraphString) Pattern)
        )
        Y_EDL_DEFAULT_MEMBER((TGlobalResourceInfo) ResourceInfo)
    };

    struct TConf {
        Y_EDL_MEMBERS(
            ((TVector<TGlobalResource>) Resources)
        )
        Y_EDL_DEFAULT_MEMBER((THashMap<TGraphString, NJson::TJsonValue>) Remainder)
    };

    struct TNodeResource {
        Y_EDL_MEMBERS(
            ((TGraphString) Uri)
        )
    };
    using TNodeResources = TVector<TNodeResource>;

    struct TCmd {
        Y_EDL_MEMBERS(
            ((TVector<TGraphString>) CmdArgs),
            ((TEnv) Env),
            ((TFilePath) Cwd),
            ((TFilePath) Stdout)
        )
    };

    enum class EModuleType {
        Undefined,
        Bundle /* "bundle" */,
        Library /* "lib" */,
        Program /* "bin" */,
        Dll /* "so" */,
    };

    struct TTargetProperties {
        Y_EDL_MEMBERS(
            ((EModuleType) ModuleType),
            ((TGraphString) Owner),
            ((TGraphString) ModuleTag),
            ((TGraphString) ModuleLang),
            ((TGraphString) ModuleDir)
        )
    };

    struct TForeignDeps {
        Y_EDL_MEMBERS(
            ((TVector<TUid>) Tool)
        )
    };

    struct TNode : public TThrRefBase {
        Y_EDL_MEMBERS(
            ((bool) Backup),
            ((bool) HostPlatform),
            ((bool) Sandboxing),
            ((i8) Type),
            ((TMaybe<bool>) Broadcast),
            ((TMaybe<bool>) Cache),
            ((TMaybe<bool>) StableDirOutputs),
            ((i32) Timeout),
            ((TMaybe<i32>) Priority),
            ((TEnv) Env),
            ((TFilePath) Cwd),
            ((THolder<TForeignDeps>) ForeignDeps),
            ((TTargetProperties) TargetProperties),
            ((TTinyMap<TGraphString, NJson::TJsonValue>) Kv, "", NYa::NEdl::EMemberExportPolicy::ALWAYS),
            ((TGraphString) NodeType, "node-type"),
            ((TGraphString) Platform),
            ((TGraphString) Tag),
            ((TNodeResources) Resources),
            ((TOutputs) DirOutputs),
            ((TOutputs) Outputs, "", NYa::NEdl::EMemberExportPolicy::ALWAYS),
            ((TTinyMap<TGraphString, NJson::TJsonValue>) Requirements, "", NYa::NEdl::EMemberExportPolicy::ALWAYS),
            ((TUid) SelfUid),
            ((TUid) StatsUid),
            ((TUid) Uid),
            ((TVector<TCmd>) Cmds, "", NYa::NEdl::EMemberExportPolicy::ALWAYS),
            ((TVector<TFilePath>) Inputs, "", NYa::NEdl::EMemberExportPolicy::ALWAYS),
            ((TVector<TFilePath>) TaredOutputs),
            ((TVector<TGraphString>) Secrets),
            ((TVector<TGraphString>) Tags),
            ((TVector<TUid>) Deps, "", NYa::NEdl::EMemberExportPolicy::ALWAYS),
            ((TVector<TUid>) Tools) // For compatibility with old graph version
        )

        Y_EDL_DEFAULT_MEMBER((TTinyMap<TGraphString, NJson::TJsonValue>) OtherAttrs)

        inline bool IsBinary() const {
            return TargetProperties.ModuleType == EModuleType::Program;
        }

        inline bool IsBundle() const {
            return TargetProperties.ModuleType == EModuleType::Bundle;
        }

        inline bool IsDynLibrary() const {
            return TargetProperties.ModuleType == EModuleType::Dll;
        }
    };

    using TNodePtr = TIntrusivePtr<TNode>;
    using TNodeList = TVector<TNodePtr>;

    inline TNodePtr CreateNode() {
        return MakeIntrusive<TNode>();
    }

    struct TGraph : public TThrRefBase, public TNonCopyable {
        Y_EDL_MEMBERS(
            ((TConf) Conf),
            ((TNodeList) Graph, "", NEdl::EMemberExportPolicy::ALWAYS),
            ((TVector<TUid>) Result, "", NEdl::EMemberExportPolicy::ALWAYS),
            ((THashMap<TGraphString, std::array<ui64, 2>>) Inputs, "", NEdl::EMemberExportPolicy::ALWAYS)
        )

        size_t Size() const;
        void AddGlobalResources(const TVector<TGlobalResource>& resources);
        void SetTags(const TVector<TGraphString>& tags);
        void SetPlatform(const TGraphString platform);
        void AddHostMark(bool sandboxing);
        void AddToolDeps();
        void AddSandboxingMark();
        void UpdateConf(const TConf& conf);
        void Strip();
    };

    using TGraphPtr = TIntrusivePtr<TGraph>;

    inline TGraphPtr CreateGraph() {
        return MakeIntrusive<TGraph>();
    }

    struct TGraphMergingResult {
        TGraphPtr Merged;
        TVector<TString> UnmatchedTargetToolDirs;
    };

    TGraphMergingResult MergeGraphs(TGraphPtr tools, TGraphPtr pic, TGraphPtr noPic);
    TGraphMergingResult MergeSingleGraph(TGraphPtr tools, TGraphPtr target);

    void CleanStorage();
}

namespace NYa::NEdl {
    template <>
    struct TEmptyChecker<NYa::NGraph::EModuleType> {
        static bool IsEmpty(const NYa::NGraph::EModuleType& val) {
            return val == NYa::NGraph::EModuleType::Undefined;
        }
    };
}
