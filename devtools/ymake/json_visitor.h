#pragma once

#include "json_entry_stats.h"
#include "module_restorer.h"
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

class TUidsData : public TManagedPeerVisitor<TJSONEntryStats, TJsonStateItem>, public TUidsCachable {
protected:
    using TBaseVisitor = TManagedPeerVisitor<TJSONEntryStats, TJsonStateItem>;

    TGraphLoops Loops;
    TNodesData<TLoopCnt, TVector> LoopCnt;
    NStats::TUidsCacheStats CacheStats{"Uids cache stats"};

public:
    using TBaseVisitor::Nodes;

    TUidsData(const TRestoreContext& restoreContext, const TVector<TTarget>& startDirs);
    virtual ~TUidsData() = default;

    virtual void SaveCache(IOutputStream* output, const TDepGraph& graph) override;
    virtual void LoadCache(IInputStream* input, const TDepGraph& graph) override;

private:
    void SaveLoop(TSaveBuffer* buffer, TNodeId loopId, const TDepGraph& graph);
    bool LoadLoop(TLoadBuffer* buffer, TNodeId nodeFromLoop, const TDepGraph& graph);
};


class TJSONVisitor final : public TUidsData {
private:
    using TUidsData::TBaseVisitor;
    using TNodeData = TJSONEntryStats;

    const TCommands& Commands;
    const TCmdConf& CmdConf;

    const bool MainOutputAsExtra;
    const bool JsonDepsFromMainOutputEnabled_ = false;

    ui64 NumModuleNodesForRendering = 0;
    TVector<TNodeId> SortedNodesForRendering;
    TMap<size_t, TVector<TNodeId>> TopoGenerations;

    TVector<TLoopCnt> LoopsHash;

    TVector<std::pair<ui32, TMd5Sig>> Inputs;
    THashMap<TNodeId, TSimpleSharedPtr<TUniqVector<TNodeId>>> NodesInputs;
    THashMap<TNodeId, TSimpleSharedPtr<TUniqVector<TNodeId>>> LoopsInputs;
    TVector<TString> HostResources;
    THashMap<TString, TString> Resources;
    THashMap<TNodeId, TNodeId> Node2Module;
    THashSet<TNodeId> StartModules;
    TGlobalVarsCollector GlobalVarsCollector;

    bool HasParent = false;
    TDepGraph::TConstEdgeRef Edge;
    TDepGraph::TConstNodeRef CurrNode;
    TNodeData* CurrData = nullptr;
    TNodeData* PrntData = nullptr;
    TStateItem* CurrState = nullptr;
    TStateItem* PrntState = nullptr;
    EMakeNodeType CurrType = EMNT_Last;
    const TDepGraph& Graph;

public:
    TJSONVisitor(const TRestoreContext& restoreContext, TCommands& commands, const TCmdConf& cmdConf, const TVector<TTarget>& startDirs);

    using TBaseVisitor::Nodes;

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
    const TMap<size_t, TVector<TNodeId>>& GetTopoGenerations() const {
        TStringStream ss;
        ss << "GetTopoGenerations: ";
        for (const auto& [_, nodes] : TopoGenerations) {
            ss << "  " << nodes.size();
        }
        YDebug() << ss.Str() << Endl;
        return TopoGenerations;
    }
    ui64 GetModuleNodesNum() const { return NumModuleNodesForRendering; }

    void ReportCacheStats();

    TErrorShowerState ErrorShower;

private:
    void PrepareLeaving(TState& state);

    // Node was entered first time, no children visited
    void PrepareCurrent(TState& state);
    // Node was leaved first time, all children visited and finished
    void FinishCurrent(TState& state);
    // Returning from a child node. The child node should be finished, while the parent is not.
    void PassToParent(TState& state);

    bool NeedAddToOuts(const TState& state, const TDepTreeNode& node) const;

    void UpdateParent(TState& state, TStringBuf value, TStringBuf description);
    void UpdateParent(TState& state, const TMd5SigValue& value, TStringBuf description);

    void UpdateCurrent(TState& state, TStringBuf value, TStringBuf description);

    void AddAddincls(TState& state);
    void AddGlobalVars(TState& state);
    void AddGlobalSrcs();

    void ComputeLoopHash(TNodeId loopId);

    void UpdateReferences(TState& state);
    void CheckStructureUidChanged(const TJSONEntryStats& data);
};
