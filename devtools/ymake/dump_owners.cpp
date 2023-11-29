#include "dump_owners.h"
#include "prop_names.h"

#include "ymake.h"

#include <devtools/ymake/compact_graph/query.h>


bool TOwnersPrinter::AcceptDep(TState& state) {
    bool result = TBase::AcceptDep(state);
    auto dep = state.NextDep();
    bool isStartModuleDep = !state.HasIncomingDep() && IsDirToModuleDep(dep);
    if (isStartModuleDep && !ModuleStartTargets.contains(dep.To().Id())) {
        return false;
    }
    if (IsMakeFilePropertyDep(dep.From()->NodeType, dep.Value(), dep.To()->NodeType)) {
        ui64 propId;
        TStringBuf propType, propValue;
        ParseCommandLikeProperty(TDepGraph::GetCmdName(dep.To()).GetStr(), propId, propType, propValue);
        if (EqualToOneOf(propType, NProps::RECURSES, NProps::TEST_RECURSES)) {
            return false;
        }
    }
    return result;
}

void TOwnersPrinter::Leave(TState& state) {
    TStateItem& st = state.Top();
    TNodeId leftNode = st.Node().Id();
    auto leftType = st.Node()->NodeType;
    IsMod = IsModule(st);

    if (leftType == EMNT_Directory) {
        TNodeId leftOwner = Nodes[leftNode].Owner;
        if (leftOwner != 0) {
            for (auto modId : Nodes[leftNode].ModIds) {
                Module2Owner[modId] = leftOwner;
            }
        }
    }

    TBase::Leave(state);
}

void TOwnersPrinter::Left(TState& state) {
    TBase::Left(state);
    TStateItem& st = state.Top();
    auto nodeType = st.Node()->NodeType;
    auto leftNode = st.CurDep().To();
    auto leftType = leftNode->NodeType;

    if (nodeType == EMNT_MakeFile) {
        if (leftType == EMNT_Property) {
            CurEnt->Owner = leftNode.Id();
        }
    } else if (nodeType == EMNT_Directory) {
        if (IsMod) {
            CurEnt->ModIds.push_back(leftNode.Id());
        } else if (leftType == EMNT_MakeFile) {
            if (Nodes[leftNode.Id()].Owner != 0) {
                CurEnt->Owner = Nodes[leftNode.Id()].Owner;
            }
        }
    }
}

void TYMake::DumpOwners() {
    TOwnersPrinter printer(ModuleStartTargets);
    IterateAll(Graph, StartTargets, printer, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });

    Conf.Cmsg() << "****Dump owners****" << Endl;
    for (const auto& item : printer.Module2Owner) {
        Conf.Cmsg() << Graph.GetFileName(Graph.Get(item.first)) << "->" << Graph.GetCmdName(Graph.Get(item.second)) << Endl;
    }
}
