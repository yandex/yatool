#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/dep_types.h>

#include <util/ysaveload.h>

#include "induced_props.h"
#include "node_debug.h"

enum class EIterType {
    NotSet,
    MainIter,
    ReIter
};

namespace NDebugEvents::NIter {
    struct TEnterEvent {
        TNodeIdLog Node;
        EDepType Edge = EDT_Last;
        EIterType IterType = EIterType::NotSet;

        Y_SAVELOAD_DEFINE(Node, Edge, IterType);

        TEnterEvent() = default;
        TEnterEvent(const TDepGraph& graph, const TDepTreeNode& node, EDepType edge, EIterType iterType);
    };

    struct TLeaveEvent {
        TNodeIdLog Node;
        EIterType IterType = EIterType::NotSet;

        Y_SAVELOAD_DEFINE(Node, IterType);

        TLeaveEvent() = default;
        TLeaveEvent(const TDepGraph& graph, const TDepTreeNode& node, EIterType type);
    };

    struct TLeftEvent {
        TNodeIdLog FromNode;
        TNodeIdLog ToNode;
        EIterType IterType = EIterType::NotSet;

        Y_SAVELOAD_DEFINE(FromNode, ToNode, IterType);

        TLeftEvent() = default;
        TLeftEvent(const TDepGraph& graph, TDepsCacheId fromNode, TDepsCacheId toNode, EIterType iterType);
    };

    struct TLeftEventLogEventGuard {
        TDepGraph* Graph;
        TDepsCacheId From;
        TDepsCacheId To;
        EIterType IterType;

        void Log() {
    #if !defined(NDEBUG)
            if (Graph) {
                BINARY_LOG(Iter, NIter::TLeftEvent, *Graph, From, To, IterType);
                Graph = nullptr;
            }
    #endif
        }

        ~TLeftEventLogEventGuard() {
            Log();
        }
    };

    struct TRawPopEvent {
        TNodeIdLog Node;

        Y_SAVELOAD_DEFINE(Node);

        TRawPopEvent() = default;
        TRawPopEvent(const TDepGraph& graph, const TDepTreeNode& node);
    };

    struct TStartEditEvent {
        TNodeIdLog Node;
        bool IsEdited;

        Y_SAVELOAD_DEFINE(Node, IsEdited);

        TStartEditEvent() = default;
        TStartEditEvent(const TDepGraph& graph, const TDepTreeNode& node, bool isEdited);
    };

    struct TRescanEvent {
        TNodeIdLog Node;

        Y_SAVELOAD_DEFINE(Node);

        TRescanEvent() = default;
        TRescanEvent(const TDepGraph& graph, TDepTreeNode node);
    };

    enum class ENotReadyLocation {
        Constructor,
        NodeToProps,
        ReIterNodeToProps,
        MissingFromChild,
        ReIterLeave,
        NeedUpdate,
        StartEdit,
        Custom,
    };

    struct TNotReadyIntents {
        TNodeIdLog Node;
        TIntents PreviousIntents;
        TIntents Intents;
        ENotReadyLocation Location;

        Y_SAVELOAD_DEFINE(Node, PreviousIntents, Intents, Location);

        TNotReadyIntents() = default;
        TNotReadyIntents(const TDepGraph& graph, TDepsCacheId node, TIntents previousIntents, TIntents intents, ENotReadyLocation location);
    };

    struct TSetupRequiredIntents {
        TNodeIdLog Node;
        TIntents Intents;

        Y_SAVELOAD_DEFINE(Node, Intents);

        TSetupRequiredIntents() = default;
        TSetupRequiredIntents(const TDepGraph& graph, TDepsCacheId node, TIntents intents);
    };

    struct TSetupReceiveFromChildIntents {
        TNodeIdLog Node;
        EMakeNodeType ChildNodeType;
        EDepType Edge;
        TIntents Intents;

        Y_SAVELOAD_DEFINE(Node, ChildNodeType, Edge, Intents);

        TSetupReceiveFromChildIntents() = default;
        TSetupReceiveFromChildIntents(const TDepGraph& graph, TDepsCacheId node, EMakeNodeType childNodeType, EDepType edge, TIntents intents);
    };

    enum class EFetchIntentLocation {
        IterEnter,
        ReIterEnter,
    };

    struct TResetFetchIntents {
        TNodeIdLog Node;
        TIntents Intents;
        EFetchIntentLocation Location;

        Y_SAVELOAD_DEFINE(Node, Intents, Location);

        TResetFetchIntents() = default;
        TResetFetchIntents(const TDepGraph& graph, TDepsCacheId node, TIntents intents, EFetchIntentLocation location);
    };

} // namespace NDebugEvents::NIter
