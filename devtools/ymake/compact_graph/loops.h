#pragma once

#include "dep_graph.h"
#include "nodes_data.h"

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

class TGraphLoops: public TNodesData<TGraphLoop, TVector>, public TMoveOnly {
private:
    THashSet<TNodeId> DirLoops;
    THashSet<TNodeId> BuildLoops;
    THashMap<TNodeId, TNodeId> Node2Loop;

public:
    bool HasBadLoops() const;
    THashSet<TNodeId> GetNodesToRemove(TDepGraph& graph, TVector<TTarget>& startTargets) const;
    void RemoveBadLoops(TDepGraph& graph, TVector<TTarget>& startTargets) const;
    const TNodeId* FindLoopForNode(TNodeId node) const {return Node2Loop.FindPtr(node);}

    static TGraphLoops Find(const TDepGraph& graph, const TVector<TTarget> startTargets, bool outTogetherIsLoop);

    void DumpAllLoops(const TDepGraph& graph, IOutputStream& out) const;
    void DumpBuildLoops(const TDepGraph& graph, IOutputStream& out) const;
    void DumpDirLoops(const TDepGraph& graph, IOutputStream& out) const;

private:
    TGraphLoops(
        TNodesData<TGraphLoop, TVector>&& loop2nodes,
        THashSet<TNodeId>&& dirLoops,
        THashSet<TNodeId>&& buildLoops,
        THashMap<TNodeId, TNodeId>&& node2loop) noexcept
        : TNodesData<TGraphLoop, TVector>{std::move(loop2nodes)}
        , DirLoops{std::move(dirLoops)}
        , BuildLoops{std::move(buildLoops)}
        , Node2Loop{std::move(node2loop)}
    {}

    template <typename TContainer>
    void DumpLoops(const TDepGraph& graph, IOutputStream& out, const TContainer& loopIds) const;
};
