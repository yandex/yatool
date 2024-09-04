#include "action.h"

bool IsMainOutput(const TDepGraph& graph, TNodeId currNode, TNodeId mainNode) {
    TStringBuf currName = graph.ToTargetStringBuf(currNode);
    TStringBuf mainName = graph.ToTargetStringBuf(mainNode);
    return currName == mainName;
}
