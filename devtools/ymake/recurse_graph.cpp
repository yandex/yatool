#include "macro_string.h"
#include "prop_names.h"
#include "recurse_graph.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>


TMineRecurseVisitor::TMineRecurseVisitor(const TDepGraph& graph, TDepGraph& recurseGraph)
: Graph(graph)
, RecurseGraph(recurseGraph)
{}

bool TMineRecurseVisitor::AcceptDep(TState& state) {
    bool result = TBase::AcceptDep(state);
    const auto& dep = state.NextDep();
    return result && (dep.To()->NodeType == EMNT_MakeFile || IsModulePropertyDep(dep.From()->NodeType, dep.Value(),dep.To()->NodeType) || IsMakeFilePropertyDep(dep.From()->NodeType, dep.Value(),dep.To()->NodeType) || IsDirToModuleDep(dep));
}

bool TMineRecurseVisitor::Enter(TState& state) {
    if (!TBase::Enter(state)) {
        return false;
    }
    auto topNode = state.TopNode();
    if (IsModule(state.Top())) {
        auto addedNode = RecurseGraph.AddNode(topNode->NodeType, topNode->ElemId);
        Y_ASSERT(state.HasIncomingDep());
        auto parent = state.ParentNode();
        Y_ASSERT(parent->NodeType == EMNT_Directory);
        auto parentNode = RecurseGraph.GetNodeById(parent->NodeType, parent->ElemId);
        RecurseGraph.AddEdge(parentNode, addedNode.Id(), EDT_Include);
    }
    if (state.HasIncomingDep()) {
        auto parent = state.ParentNode();
        bool isMakeFileProperty = IsMakeFilePropertyDep(parent->NodeType, state.IncomingDep().Value(), topNode->NodeType);
        bool isModuleProperty = IsModulePropertyDep(parent->NodeType, state.IncomingDep().Value(), topNode->NodeType) && topNode->NodeType == EMNT_BuildCommand;

        if (isMakeFileProperty || isModuleProperty) {
            ui64 propId;
            TStringBuf propType, propValue;
            ParseCommandLikeProperty(Graph.GetCmdName(topNode).GetStr(), propId, propType, propValue);
            if (EqualToOneOf(propType, NProps::RECURSES, NProps::TEST_RECURSES, NProps::DEPENDS)) {
                auto node = Graph.Get(topNode.Id());
                for (auto edge : node.Edges()) {
                    if (edge.To()->NodeType == EMNT_Directory) {
                        auto addedNode = RecurseGraph.AddNode(edge.To()->NodeType, edge.To()->ElemId);
                        auto topDir = *(state.end() - 1);
                        Y_ASSERT(topDir.Node()->NodeType == EMNT_Directory);
                        auto topDirNode = RecurseGraph.GetNodeById(topDir.Node()->NodeType, topDir.Node()->ElemId);
                        RecurseGraph.AddEdge(topDirNode, addedNode.Id(), EDT_Include);
                    }
                }
            }
            return false;
        }
    }
    else if (topNode->NodeType == EMNT_Directory) {
        RecurseGraph.AddNode(topNode->NodeType, topNode->ElemId).Id();
    }
    return true;
}

TFilterRecurseVisitor::TFilterRecurseVisitor(const TDepGraph& graph, const TVector<TTarget>& startTargets, TDepGraph& recurseGraph)
: Graph(graph)
, RecurseGraph(recurseGraph)
{
    for (auto target : startTargets) {
        if (target.IsModuleTarget) {
            ModuleStartTargets.insert(Graph.Get(target.Id)->ElemId);
        }
    }
}

bool TFilterRecurseVisitor::Enter(TState& state) {
    bool result = TBase::Enter(state);
    if (result) {
        auto topNode = state.TopNode();
        if (IsModuleType(topNode->NodeType)) {
            if (ModuleStartTargets.contains(topNode->ElemId)) {
                CurEnt->NotRemove = true;
            }
        }
        else {
            auto graphNode = RecurseGraph.GetNodeById(topNode->NodeType, topNode->ElemId);
            if (graphNode.Edges().IsEmpty()) {
                CurEnt->NotRemove = true;
            }
        }
    }
    return result;
}

void TFilterRecurseVisitor::Leave(TState& state) {
    TBase::Leave(state);
    if (!CurEnt->NotRemove) {
        auto recurseNode = state.TopNode();
        FilteredNodes.insert(recurseNode.Id());
    }
}

void TFilterRecurseVisitor::Left(TState& state) {
    auto prevEnt = CurEnt;
    TBase::Left(state);
    if (prevEnt->NotRemove) {
        CurEnt->NotRemove = true;
    }
}
