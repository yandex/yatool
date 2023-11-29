#include "induced_props_debug.h"

#include "induced_props.h"

namespace NDebugEvents::NProperties {
    TAddEvent::TAddEvent(
        const TDepGraph& graph,
        TDepsCacheId node,
        TDepsCacheId sourceNode,
        TPropertyType propType,
        TDepsCacheId propNode,
        bool isNew,
        EPropertyAdditionType additionType
    )
        : Node(graph, node)
        , SourceNode(graph, sourceNode)
        , PropType(graph, propType)
        , PropNode(graph, propNode)
        , IsNew(isNew)
        , AdditionType(additionType)
    {
    }

    TClearEvent::TClearEvent(const TDepGraph& graph, TDepsCacheId id, TPropertyType type)
        : Node(graph, id), PropType(graph, type)
    {
    }

    TReadEvent::TReadEvent(const TDepGraph& graph, TDepsCacheId id, TPropertyType type)
        : Node(graph, id), PropType(graph, type)
    {
    }

    TUseEvent::TUseEvent(const TDepGraph& graph, TDepsCacheId userNode, TDepsCacheId sourceNode, TPropertyType propType, TDepsCacheId propNode, TString note)
        : UserNode(graph, userNode), SourceNode(graph, sourceNode), PropType(graph, propType), PropNode(graph, propNode), Note(note)
    {
    }
}
