#pragma once

#include <devtools/ymake/compact_graph/loops.h>

void PropagateChangeFlags(TDepGraph& graph, const TGraphLoops& loops, TVector<TTarget>& startTargets);
void SetLocalChangesForGrandBypass(TDepGraph& graph, const TVector<TTarget>& startTargets);
