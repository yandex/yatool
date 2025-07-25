#include "global_vars_collector.h"

#include "module_state.h"
#include "module_store.h"
#include "module_restorer.h"
#include "macro_processor.h"
#include "mine_variables.h"

#include <devtools/ymake/compact_graph/query.h>


bool TGlobalVarsCollector::Start(const TStateItem& parentItem) {
    TModule& parent = InitModule(RestoreContext.Modules, RestoreContext.Conf.CommandConf, parentItem.Node());
    auto& globalVars = RestoreContext.Modules.GetGlobalVars(parent.GetId());
    if (globalVars.IsVarsComplete()) {
        return false;
    }
    globalVars.ResetRoots({RestoreContext.Conf});
    return true;
}

void TGlobalVarsCollector::Finish(const TStateItem& parentItem, TEntryStatsData* parentData) {
    // ideally, we would've relied on `parentData->StructCmdDetected`,
    // but not all iterators bother updating it,
    // so instead we repeat the detection logic right here, right now
    auto _quasi_parentData_StructCmdDetected = false;
    Y_UNUSED(parentData);

    auto& parentVars = RestoreContext.Modules.GetGlobalVars(parentItem.Node()->ElemId);
    for (const auto& dep : parentItem.Node().Edges()) {
        if (dep.To()->NodeType == EMNT_BuildCommand && dep.Value() == EDT_BuildCommand) {
            if (TDepGraph::GetCmdName(dep.To()).IsNewFormat()) {
                _quasi_parentData_StructCmdDetected = true;
            }
            continue;
        }
        if (!IsBuildCmdInclusion(dep)) {
            continue;
        }
        TStringBuf depName = TDepGraph::GetCmdName(dep.To()).GetStr();
        TStringBuf varName = GetCmdName(depName);
        if (RestoreContext.Conf.CommandConf.IsReservedName(varName) || varName.EndsWith("_RESOURCE_GLOBAL")) {
            auto& vars = parentVars.GetVars();
            TVars commandInfoVars(&vars);
            TCommandInfo commandInfo(RestoreContext.Conf, &RestoreContext.Graph, nullptr);
            TUniqVector<TNodeId> lateOuts;
            MineVariables(RestoreContext.Conf, dep.To(), commandInfo.ToolPaths, commandInfo.ResultPaths, commandInfoVars, lateOuts, RestoreContext.Modules);
            TString objd = commandInfo.SubstMacro(nullptr, depName, ESM_DoSubst, commandInfoVars, ECF_Unset, true);
            auto& var = vars[varName];
            if (varName.EndsWith("_RESOURCE_GLOBAL") && !var.empty()) {
                if (objd != var.back().Name) {
                    YConfErr(Misconfiguration) << "Resource [[alt1]]" << varName << "[[rst]] is declared several times with different definition";
                }
            } else {
                var.push_back(TVarStr(objd));
                var.back().StructCmdForVars = _quasi_parentData_StructCmdDetected;
            }
        }
    }
    parentVars.SetVarsComplete();
}

void TGlobalVarsCollector::Collect(const TStateItem& parentItem, TConstDepNodeRef peerNode) {
    TModule* peer = RestoreContext.Modules.Get(peerNode->ElemId);
    Y_ASSERT(peer);
    if (!peer->PassPeers()) {
        return;
    }
    if (peer->GetAttrs().RequireDepManagement && RestoreContext.Modules.Get(parentItem.Node()->ElemId)->GetAttrs().RequireDepManagement) {
        // Vars propagation between manageable peers can not be cached on each peerdir dep. It must be performed on the resulted
        // list of peers closuere after dependency management applied.
        // GlobalVars for DM aware module contains values that must be induced on consumers rather then complete closure of self
        // values and values induced by peers.
        return;
    }
    const auto& peerVars = RestoreContext.Modules.GetGlobalVars(peer->GetId()).GetVars();
    auto& parentVars = RestoreContext.Modules.GetGlobalVars(parentItem.Node()->ElemId).GetVars();
    for (const auto& peerVar : peerVars) {
        parentVars[peerVar.first].AppendUnique(peerVar.second);
    }
}
