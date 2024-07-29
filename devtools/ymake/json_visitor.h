#pragma once

#include "module_restorer.h"
#include "json_entry_stats.h"

#include "json_visitor_new.h"

#include "saveload.h"

#include <devtools/ymake/command_store.h>
#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/loops.h>
#include <devtools/ymake/global_vars_collector.h>
#include <devtools/ymake/make_plan/resource_section_params.h>
#include <devtools/ymake/managed_deps_iter.h>

#include <devtools/libs/yaplatform/platform_map.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/deprecated/autoarray/autoarray.h>

#include <util/generic/fwd.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

#include <utility>

class TBuildConfiguration;
class TModules;
class TModule;
class TYMake;

class TSaveBuffer;
class TLoadBuffer;


class TJSONVisitor final : public TJSONVisitorNew, public TUidsCachable {
protected:
    using TBase = TJSONVisitorNew;

private:
    ui64 NumModuleNodesForRendering = 0;
    TVector<TNodeId> SortedNodesForRendering;

    autoarray<TLoopCnt> LoopCnt;

    TVector<std::pair<ui32, TMd5Sig>> Inputs;
    THashMap<TNodeId, TSimpleSharedPtr<TUniqVector<TNodeId>>> NodesInputs;
    THashMap<TNodeId, TSimpleSharedPtr<TUniqVector<TNodeId>>> LoopsInputs;
    TVector<TString> HostResources;
    THashMap<TString, TString> Resources;
    THashMap<TNodeId, TNodeId> Node2Module;
    THashSet<TNodeId> StartModules;
    TGlobalVarsCollector GlobalVarsCollector;

    const bool JsonDepsFromMainOutputEnabled_ = false;

public:
    TJSONVisitor(const TRestoreContext& restoreContext, TCommands& commands, const TCmdConf& cmdConf, const TVector<TTarget>& startDirs, bool newUids);

    using TBase::Nodes;

    virtual void SaveCache(IOutputStream* output, const TDepGraph& graph) override;
    virtual void LoadCache(IInputStream* input, const TDepGraph& graph) override;

    // Iteration.
    bool AcceptDep(TState& state);
    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);

    // Results.
    THashMap<TString, TMd5Sig> GetInputs(const TDepGraph& graph) const;
    TSimpleSharedPtr<TUniqVector<TNodeId>>& GetNodeInputs(TNodeId node);
    const TVector<TString>& GetHostResources() const;
    const THashMap<TString, TString>& GetResources() const;
    TNodeId GetModuleByNode(TNodeId nodeId);
    const TVector<TNodeId>& GetOrderedNodes() const { return SortedNodesForRendering; }
    ui64 GetModuleNodesNum() const { return NumModuleNodesForRendering; }

public:
    TErrorShowerState ErrorShower;

protected:
    void PrepareLeaving(TState& state);

private:
    void SaveLoop(TSaveBuffer* buffer, TNodeId loopId, const TDepGraph& graph);
    bool LoadLoop(TLoadBuffer* buffer, TNodeId nodeFromLoop, const TDepGraph& graph);

    bool NeedAddToOuts(const TState& state, const TDepTreeNode& node) const;
};
