#include "dep_graph.h"
#include <devtools/ymake/diag/stats.h>

template <>
void Out<TDepTreeNode>(IOutputStream& os, TTypeTraits<TDepTreeNode>::TFuncParam v) {
    os << "(" << v.ElemId << " " << ToString(v.NodeType) << ")";
}

void TDepGraph::ReportStats() const {
    NStats::TDepGraphStats stats{"DepGraph stats"};
    for (const auto& node : Nodes()) {
        stats.Inc(NStats::EDepGraphStats::NodesCount);
        stats.Inc(NStats::EDepGraphStats::EdgesCount, node.Edges().Total());
    }
    stats.Set(NStats::EDepGraphStats::FilesCount, Names().FileConf.Size());
    stats.Set(NStats::EDepGraphStats::CommandsCount, Names().CommandConf.Size());
    stats.Report();
}
