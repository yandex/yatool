#include "add_iter_debug.h"

namespace NDebugEvents::NIter {
    TEnterEvent::TEnterEvent(const TDepGraph& graph, const TDepTreeNode& node, EDepType edge, EIterType iterType)
        : Node(graph, node) , Edge(edge) , IterType(iterType)
    {
    }

    TLeaveEvent::TLeaveEvent(const TDepGraph& graph, const TDepTreeNode& node, EIterType type)
        : Node(graph, node) , IterType(type)
    {
    }

    TLeftEvent::TLeftEvent(const TDepGraph& graph, TDepsCacheId fromNode, TDepsCacheId toNode, EIterType iterType)
        : FromNode(graph, fromNode), ToNode(graph, toNode), IterType(iterType)
    {
    }

    TRawPopEvent::TRawPopEvent(const TDepGraph& graph, const TDepTreeNode& node)
        : Node(graph, node)
    {
    }

    TStartEditEvent::TStartEditEvent(const TDepGraph& graph, const TDepTreeNode& node, bool isEdited)
        : Node(graph, node), IsEdited(isEdited)
    {
    }

    TRescanEvent::TRescanEvent(const TDepGraph& graph, TDepTreeNode node, TIntents intents)
        : Node(graph, node), Intents(intents)
    {
    }

    TNotReadyIntents::TNotReadyIntents(const TDepGraph& graph, TDepsCacheId node, TIntents previousIntents, TIntents intents, ENotReadyLocation location)
        : Node(graph, node), PreviousIntents(previousIntents), Intents(intents), Location(location)
    {
    }

    TSetupRequiredIntents::TSetupRequiredIntents(const TDepGraph& graph, TDepsCacheId node, TIntents intents)
        : Node(graph, node), Intents(intents)
    {
    }

    TSetupReceiveFromChildIntents::TSetupReceiveFromChildIntents(const TDepGraph& graph, TDepsCacheId node, EMakeNodeType childNodeType, EDepType edge, TIntents intents)
        : Node(graph, node), ChildNodeType(childNodeType), Edge(edge), Intents(intents)
    {
    }

    TResetFetchIntents::TResetFetchIntents(const TDepGraph& graph, TDepsCacheId node, TIntents intents, EFetchIntentLocation location)
        : Node(graph, node), Intents(intents), Location(location)
    {
    }
}
