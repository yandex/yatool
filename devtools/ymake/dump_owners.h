#pragma once

#include <devtools/ymake/compact_graph/iter.h>

struct TOwnersEntrySt: public TEntryStats {
    TNodeId Owner;
    TVector<TNodeId> ModIds;

    TOwnersEntrySt(TItemDebug itemDebug = TItemDebug{}, bool inStack = false, bool isFile = false, TNodeId owner = 0)
        : TEntryStats(itemDebug, inStack, isFile)
        , Owner(owner)
    {
    }
};

struct TOwnersPrinter: public TNoReentryStatsConstVisitor<TOwnersEntrySt> {
    using TBase = TNoReentryStatsConstVisitor<TOwnersEntrySt>;

    const THashSet<TTarget>& ModuleStartTargets;
    THashMap<TNodeId, TNodeId> Module2Owner;
    bool IsMod;

    TOwnersPrinter(const THashSet<TTarget>& moduleStartTargets)
    : ModuleStartTargets(moduleStartTargets) {}

    bool AcceptDep(TState& state);

    void Left(TState& state);
    void Leave(TState& state);
};
