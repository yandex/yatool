#include "all_srcs_context.h"

#include <devtools/ymake/add_node_context.h>
#include <devtools/ymake/module_builder.h>
#include <devtools/ymake/add_iter.h>
#include <devtools/ymake/prop_names.h>

void TAllSrcsContext::InitializeNode(TModuleBuilder& builder) {
    auto name = FormatCmd(builder.ModuleNodeElemId, NProps::ALL_SRCS, "");
    auto nameId = builder.Graph.Names().AddName(NodeType, name);

    builder.Node.AddUniqueDep(DepType, NodeType, nameId);

    auto& [id, entryStats] = *builder.UpdIter.Nodes.Insert(
        MakeDepsCacheId(NodeType, nameId),
        &builder.UpdIter.YMake,
        &builder.Module
    );
    Node = &entryStats.GetAddCtx(&builder.Module, builder.UpdIter.YMake);
    Node->NodeType = NodeType;
    Node->ElemId = nameId;
    entryStats.SetOnceEntered(false);
    entryStats.SetReassemble(true);

    // Process items added before Node was initialized
    for (auto depNode : TemporalDepStorage) {
        Node->AddUniqueDep(EDT_BuildFrom, depNode.NodeType, depNode.ElemId);
    }
    TemporalDepStorage = {};

    YDIAG(GUpd) << "AllSrcs.Node initialized for module " << builder.ModuleNodeElemId << "\n";
}

void TAllSrcsContext::AddDep(TDepTreeNode depNode) {
    if (Node) {
        Node->AddUniqueDep(EDT_BuildFrom, depNode.NodeType, depNode.ElemId);
    } else {
        TemporalDepStorage.push_back(depNode);
    }
}

bool TAllSrcsContext::IsAllSrcsNode(const TNodeAddCtx* other) {
    if (Node == nullptr) {
        return false;
    }
    return Node == other;
}
