#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>

bool IsMainOutput(const TDepGraph& graph, TNodeId currNode, TNodeId mainNode);
