#include "sem_graph.h"

TString TSemGraph::ToString(const TSemGraph::TConstNodeRef& node) {
    return node.Value().Path;
}