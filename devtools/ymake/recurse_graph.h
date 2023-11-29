#pragma once

#include <devtools/ymake/compact_graph/iter.h>

class TDepGraph;

class TMineRecurseVisitor : public TNoReentryVisitorBase<TEntryStats, TGraphIteratorStateItemBase<true>> {
public:
    using TBase = TNoReentryVisitorBase<TEntryStats, TGraphIteratorStateItemBase<true>>;
    using TState = typename TBase::TState;

public:
    TMineRecurseVisitor(const TDepGraph& graph, TDepGraph& recurseGraph);

    bool AcceptDep(TState& state);
    bool Enter(TState& state);

private:
    const TDepGraph& Graph;
    TDepGraph& RecurseGraph;
};


struct TRecurseEntryStatsData {
    bool NotRemove = false;
};

using TRecurseEntryStats = TVisitorStateItem<TRecurseEntryStatsData>;
class TFilterRecurseVisitor : public TNoReentryVisitorBase<TRecurseEntryStats, TGraphIteratorStateItemBase<true>> {
public:
    using TBase = TNoReentryVisitorBase<TRecurseEntryStats, TGraphIteratorStateItemBase<true>>;
    using TState = typename TBase::TState;

public:
    TFilterRecurseVisitor(const TDepGraph& graph, const TVector<TTarget>& startTargets, TDepGraph& recurseGraph);

    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);

    const THashSet<TNodeId>& GetFilteredNodes() const {
        return FilteredNodes;
    }

private:
    const TDepGraph& Graph;
    TDepGraph& RecurseGraph;
    THashSet<ui32> ModuleStartTargets;
    THashSet<TNodeId> FilteredNodes;
};