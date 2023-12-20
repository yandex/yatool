#pragma once

#include <string>

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/iter.h>

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>

namespace NYexport {

using TNodeSemantic = TVector<std::string>; ///< One semantic function: *name, args...
using TNodeSemantics = TVector<TNodeSemantic>; ///< List of semantic functions

struct TSemNodeData {
    TString Path;
    EMakeNodeType NodeType = EMNT_Deleted;
    TNodeSemantics Sem;
};

using TSemDepData = jinja2::ValuesMap;

using TSemDepId = ui64;
using TSemDepId2Data = THashMap<TSemDepId, TSemDepData>;

constexpr std::string_view TOOL_NODES_FAKE_PATH = "TOOL";
constexpr std::string_view TESTS_PROPERTY_FAKE_PATH = "TESTS";

inline static const std::string DEPATTR_EXCLUDES = "Excludes:[NodeId]";
inline static const std::string DEPATTR_IS_CLOSURE = "IsClosure:bool";

using TSemCompactEdge = TCompactEdge<EDepType, 28>;

class TSemGraph: public TCompactGraph<EDepType, TSemNodeData, TSemCompactEdge> {
public:
    using TBase = TCompactGraph<EDepType, TSemNodeData, TSemCompactEdge>;
    static TString ToString(const TSemGraph::TConstNodeRef& node);

    static TBase::TConstEdgeRef GetInvalidEdge(const TBase::TConstNodeRef& node) {
        return static_cast<const TSemGraph&>(node.Graph()).TBase::GetInvalidEdge();
    }

    void SetDepData(TBase::TConstEdgeRef dep, TSemDepData&& semDepData) {
        SetDepData(SemDepId(dep), std::move(semDepData));
    }

    const TSemDepData* GetDepData(TBase::TConstEdgeRef dep) const {
        return GetDepData(SemDepId(dep));
    }

    /// Compact internal representation of edge, can be used as id of edge
    static TSemDepId SemDepId(TBase::TConstEdgeRef dep) {
        return static_cast<TSemDepId>(TSemCompactEdge{dep.To().Id(), dep.Value()}.Representation())
            | (static_cast<TSemDepId>(dep.From().Id()) << sizeof(TNodeId) * 8u);
    }

private:
    TSemDepId2Data DepId2Data_;

    void SetDepData(TSemDepId semDepId, TSemDepData&& semDepData) {
        DepId2Data_.insert_or_assign(semDepId, std::move(semDepData));
    }

    const TSemDepData* GetDepData(TSemDepId semDepId) const;
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
