#include "sem_graph.h"

namespace NYexport {

TString TSemGraph::ToString(const TSemGraph::TConstNodeRef& node) {
    return node.Value().Path;
}

}
