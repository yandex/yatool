#pragma once

#include "json_entry_stats.h"

#include "command_store.h"
#include "managed_deps_iter.h"

#include <devtools/ymake/compact_graph/loops.h>

#include <string_view>

struct TRestoreContext;

class TJSONVisitorNew: public TManagedPeerVisitor<TJSONEntryStats, TJsonStateItem> {
private:
    using TBase = TManagedPeerVisitor<TJSONEntryStats, TJsonStateItem>;
    using TNodeData = TJSONEntryStats;

    TCommands& Commands;

protected:
    const TCmdConf& CmdConf;

public:

    TJSONVisitorNew(const TRestoreContext& restoreContext, TCommands& commands, const TCmdConf& cmdConf, const TVector<TTarget>& startDirs, bool newUids);

    bool AcceptDep(TState& state);
    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);

    // Node was entered first time, no children visited
    void PrepareCurrent(TState& state);
    // Node was leaved first time, all children visited and finished
    void FinishCurrent(TState& state);
    // Returning from a child node. The child node should be finished, while the parent is not.
    void PassToParent(TState& state);

    bool ShouldUseNewUids() const noexcept {
        return NewUids;
    }

    void ReportCacheStats();

protected:
    TGraphLoops Loops;
    const bool NewUids;

    NStats::TUidsCacheStats CacheStats{"Uids cache stats"};

private:

    void UpdateParent(TState& state, TStringBuf value, TStringBuf description);
    void UpdateParent(TState& state, const TMd5SigValue& value, TStringBuf description);

    void UpdateCurrent(TState& state, TStringBuf value, TStringBuf description);

    void AddAddincls(TState& state);
    void AddGlobalVars(TState& state);

    void ComputeLoopHash(TNodeId loopId);

    TDepGraph::TConstEdgeRef Edge;
    TDepGraph::TConstNodeRef CurrNode;
    TNodeData* CurrData = nullptr;
    TNodeData* PrntData = nullptr;
    TStateItem* CurrState = nullptr;
    TStateItem* PrntState = nullptr;
    const TDepGraph* Graph = nullptr;

    autoarray<TLoopCnt> LoopsHash;

    void UpdateReferences(TState& state);
    void CheckStructureUidChanged();
};
