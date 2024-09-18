#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/diag/debug_values.h>

struct TNodeIdLog {
    TDepsCacheId Value = TDepsCacheId::None;
    TString String;

    TNodeIdLog() = default;
    TNodeIdLog(const TDepGraph& graph, TDepsCacheId id)
        : Value(id)
        , String(id == TDepsCacheId::None ? TString{} : graph.ToStringByCacheId(id))
    {
    }
    TNodeIdLog(const TDepGraph& graph, const TDepTreeNode& node)
        : Value(MakeDepsCacheId(node.NodeType, node.ElemId))
        , String(graph.ToStringByCacheId(Value))
    {
    }
    TNodeIdLog(const TDepGraph& graph, TNodeId node)
        : Value(MakeDepsCacheId(graph.Get(node)->NodeType, graph.Get(node)->ElemId))
        , String(graph.ToString(graph.Get(node)))
    {
    }

    inline void Save(IOutputStream* s) const {
        ::Save(s, Value);
        ::SaveDebugValue(s, String);
    }

    inline void Load(IInputStream* s) {
        ::Load(s, Value);
        ::LoadDebugValue(s, String);
    }
};

struct TGraphDebug {
    const TDepGraph& DebugGraph;
    TGraphDebug(const TDepGraph& graph) : DebugGraph(graph) {}
};

struct TNodeDebug : public TGraphDebug {
    TDepsCacheId DebugNode = TDepsCacheId{};
    TNodeDebug(const TGraphDebug& graph, TDepsCacheId id) : TGraphDebug(graph.DebugGraph), DebugNode(id) {}
    TNodeDebug(const TGraphDebug& graph, TDepTreeNode node) : TGraphDebug(graph.DebugGraph), DebugNode(MakeDepsCacheId(node.NodeType, node.ElemId)) {}
    TNodeDebug(const TDepGraph& graph, TNodeId id) : TGraphDebug(graph) {
        if (id != TNodeId::Invalid) {
            TDepGraph::TConstNodeRef node = graph.Get(id);
            DebugNode = MakeDepsCacheId(node->NodeType, node->ElemId);
        }
    }
    TNodeDebug(TDepGraph::TConstNodeRef node)
        : TGraphDebug(static_cast<const TDepGraph&>(node.Graph()))
        , DebugNode(MakeDepsCacheId(node->NodeType, node->ElemId))
    {
    }

    TString DumpDebugNode() const {
        if (DebugNode == TDepsCacheId::None) {
            return "<None>";
        }
        return DebugGraph.ToStringByCacheId(DebugNode);
    }
};

struct TNodeNonDebug {
    TString DumpDebugNode() const {
        AssertNoCall();
        return TString{};
    }
};

using TGraphDebugOnly = TDebugOnly<TGraphDebug>;
using TNodeDebugOnly = TDebugOnly<TNodeDebug, TNodeNonDebug>;
