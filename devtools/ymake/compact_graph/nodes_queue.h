#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>

#include <util/generic/queue.h>
#include <util/generic/set.h>

class TNodesQueue {
public:
    void AddEdge(const TDepTreeNode& node1, const TDepTreeNode& node2, EDepType depType);
    bool IsReachable(const TDepTreeNode& node) const;
    void MarkReachable(const TDepTreeNode& node);
    void Pop();

    TDepTreeNode GetFront() const;
    bool Empty() const;

    const TDeque<TDepTreeNode>& GetAllNodes() const;
    const TDeque<TDepTreeNode>& GetReachableNodes() const;

    const TDeque<std::pair<TDepTreeNode, EDepType>>* GetDeps(TDepTreeNode node) const;

private:
    THashMap<TDepTreeNode, TDeque<std::pair<TDepTreeNode, EDepType>>> Graph;
    TUniqContainerImpl<TDepTreeNode, TDepTreeNode, 16, TDeque<TDepTreeNode>> ReachableNodes;
    TQueue<TDepTreeNode> AvailableNodes;
    TDeque<TDepTreeNode> AllNodes;
};
