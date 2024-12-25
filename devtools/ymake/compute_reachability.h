#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>

#include <util/generic/vector.h>

namespace NComputeReachability {
    void ResetReachableNodes(TDepGraph& graph);
    void ComputeReachableNodes(TDepGraph& graph, TVector<TTarget>& startTargets);
}
