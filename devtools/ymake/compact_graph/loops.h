#pragma once

#include "dep_graph.h"

#include <util/stream/output.h>
#include <util/generic/vector.h>
#include <util/generic/hash.h>

/** Build graph has three basic types of dependencies:
 1. Build from: file content and its include set depend upon other file's include set;
 2. Include: file include set depends on other file's include set;
 3. Also Build: file include set depends on other file's content;
 It is safe to have loops of Include dependencies, while even one Build From dependency in a loop
 creates undefined build order.
 Also Build may be part of a loop only if the next dependency is a Build From, so it is a bad loop.
*/
struct TTarget;

struct TGraphLoop: public TVector<TNodeId> {
    TVector<TNodeId> Deps;
    bool DepsDone = false;
};

class TGraphLoops: public TVector<TGraphLoop> {
private:
    THashSet<TNodeId> DirLoops;
    THashSet<TNodeId> BuildLoops;

public:
    THashMap<TNodeId, TNodeId> Node2Loop;

    bool HasBadLoops() const;
    THashSet<TNodeId> GetNodesToRemove(TDepGraph& graph, TVector<TTarget>& startTargets) const;
    void RemoveBadLoops(TDepGraph& graph, TVector<TTarget>& startTargets);

    void FindLoops(const TDepGraph& graph, const TVector<TTarget>& startTargets, bool outTogetherIsLoop);
    void DumpAllLoops(const TDepGraph& graph, IOutputStream& out);
    void DumpBuildLoops(const TDepGraph& graph, IOutputStream& out);
    void DumpDirLoops(const TDepGraph& graph, IOutputStream& out);

private:
    template <typename TContainer>
    void DumpLoops(const TDepGraph& graph, IOutputStream& out, const TContainer& loopIds);
};
