#pragma once

#include <devtools/ymake/compact_graph/loops.h>

void PropagateChangeFlags(TDepGraph& graph, const TGraphLoops& loops, TVector<TTarget>& startTargets);
void InitializeLocalChangeFlags(TDepGraph& graph, const TVector<TTarget>& startTargets);
