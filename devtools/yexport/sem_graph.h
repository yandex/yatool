#pragma once

#include <string>

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/iter.h>

namespace NYexport {

using TNodeSemantic = TVector<std::string>; ///< One semantic function: *name, args...
using TNodeSemantics = TVector<TNodeSemantic>; ///< List of semantic functions

struct TSemNodeData {
    TString Path;
    EMakeNodeType NodeType = EMNT_Deleted;
    TNodeSemantics Sem;
};

constexpr std::string_view TOOL_NODES_FAKE_PATH = "TOOL";
constexpr std::string_view TESTS_PROPERTY_FAKE_PATH = "TESTS";

class TSemGraph: public TCompactGraph<EDepType, TSemNodeData, TCompactEdge<EDepType, 28>> {
public:
    using TBase = TCompactGraph<EDepType, TSemNodeData, TCompactEdge<EDepType, 28>>;
    static TString ToString(const TSemGraph::TConstNodeRef& node);

    static TBase::TConstEdgeRef GetInvalidEdge(const TBase::TConstNodeRef& node) {
        return static_cast<const TSemGraph&>(node.Graph()).TBase::GetInvalidEdge();
    }
};

template <typename TStackData, bool IsConst = true>
using TSemGraphIteratorStateItem = TGraphIteratorStateItem<TStackData, IsConst, TSemGraph>;

template <typename TIterState, typename TVisitor>
using TSemGraphDepthIterator = TDepthGraphIterator<TIterState, TVisitor, TSemGraph>;

template <typename TVisitor>
void IterateAll(const TSemGraph& graph, const TVector<TNodeId>& nodes, TVisitor& visitor) {
    typename TVisitor::TState state;
    for (TNodeId start : nodes) {
        TSemGraphDepthIterator<typename TVisitor::TState, TVisitor> it(graph, state, visitor);
        if (it.Init(graph[start])) {
            it.Run();
        }
    }
}

template <typename TVisitor>
void IterateAll(typename TVisitor::TState& state, TSemGraph::TConstNodeRef node, TVisitor& visitor) {
    TSemGraphDepthIterator<typename TVisitor::TState, TVisitor> it(static_cast<const TSemGraph&>(node.Graph()), state, visitor);
    if (it.Init(node)) {
        it.Run();
    }
}

}

template <>
inline NYexport::TSemNodeData Deleted<NYexport::TSemNodeData>(void) {
    return NYexport::TSemNodeData{};
}

template <>
inline bool Deleted(NYexport::TSemNodeData node) {
    return node.NodeType == EMNT_Deleted;
}
