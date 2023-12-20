#include "sem_graph.h"

using namespace NYexport;

TString TSemGraph::ToString(const TSemGraph::TConstNodeRef& node) {
    return node.Value().Path;
}

const TSemDepData* TSemGraph::GetDepData(TSemDepId semDepId) const {
    if (const auto semDepIt = DepId2Data_.find(semDepId); semDepIt != DepId2Data_.end()) {
        return &semDepIt->second;
    } else {
        return nullptr;
    }
}
