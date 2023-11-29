#include "nodes_queue.h"

void TNodesQueue::AddEdge(const TDepTreeNode& node1, const TDepTreeNode& node2, EDepType depType) {
    auto [it, fresh] = Graph.try_emplace(node1);
    if (fresh) {
        AllNodes.push_back(node1);
    }
    it->second.push_back(std::make_pair(node2, depType));
    if (ReachableNodes.has(node1)) {
        MarkReachable(node2);
    }
}

void TNodesQueue::Pop() {
    Y_ENSURE(!AvailableNodes.empty());
    AvailableNodes.pop();
}

void TNodesQueue::MarkReachable(const TDepTreeNode& node) {
    if (ReachableNodes.has(node)) {
        return;
    }

    TQueue<TDepTreeNode> queue({node});
    while (!queue.empty()) {
        const TDepTreeNode frontNode = queue.front();
        ReachableNodes.Push(frontNode);
        AvailableNodes.push(frontNode);
        queue.pop();

        auto depsIt = Graph.find(frontNode);
        if (depsIt != Graph.end()) {
            for (const auto& depNode : depsIt->second) {
                if (!ReachableNodes.has(depNode.first)) {
                    queue.push(depNode.first);
                }
            }
        }
    }
}

bool TNodesQueue::IsReachable(const TDepTreeNode& node) const {
    return ReachableNodes.has(node);
}

TDepTreeNode TNodesQueue::GetFront() const {
    Y_ENSURE(!AvailableNodes.empty());
    return AvailableNodes.front();
}

bool TNodesQueue::Empty() const {
    return AvailableNodes.empty();
}

const TDeque<TDepTreeNode>& TNodesQueue::GetAllNodes() const {
    return AllNodes;
}

const TDeque<TDepTreeNode>& TNodesQueue::GetReachableNodes() const {
    return ReachableNodes.Data();
}

const TDeque<std::pair<TDepTreeNode, EDepType>>* TNodesQueue::GetDeps(TDepTreeNode node) const {
    const auto it = Graph.find(node);
    if (it == Graph.end()) {
        return nullptr;
    }
    return &it->second;
}
