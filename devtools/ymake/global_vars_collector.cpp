#include "global_vars_collector.h"

#include "module_state.h"
#include "module_store.h"
#include "module_restorer.h"
#include "macro_processor.h"
#include "mine_variables.h"
#include "used_reserved_vars.h"

#include <devtools/ymake/compact_graph/query.h>

namespace {
    auto FormatURV(const TUsedReservedVars& urv) {
        TStringBuilder result;
        if (urv.FromCmd) {
            result << "fromCmd =";
            for (auto& x : *urv.FromCmd)
                result << " " << x;
        }
        if (urv.FromVars) {
            if (!result.empty())
                result << "; ";
            result << "fromVars =";
            for (auto& x : *urv.FromVars) {
                result << " " << x.first << "[";
                for (auto& y : x.second)
                    result << (&y == &*x.second.begin() ? "" : "; ") << y;
                result << "]";
            }
        }
        return result;
    };
}

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
    Y_UNUSED(parentData);

    auto& parentVars = RestoreContext.Modules.GetGlobalVars(parentItem.Node()->ElemId);
    for (const auto& dep : parentItem.Node().Edges()) {
        if (dep.To()->NodeType == EMNT_BuildCommand && dep.Value() == EDT_BuildCommand) {
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
                var.back().StructCmdForVars = true;
            }
        }
    }
    parentVars.SetVarsComplete();
}

void TGlobalVarsCollector::Finish(const TStateItem& parentItem, TJSONEntryStats* parentData) {
    Finish(parentItem, static_cast<TEntryStatsData*>(parentData));

    auto& parentVars = RestoreContext.Modules.GetGlobalVars(parentItem.Node()->ElemId);
    auto& urvLocal = parentData->UsedReservedVarsLocal;
    auto& urvTotal = parentData->UsedReservedVarsTotal;
    YDIAG(UIDs)
        << "TGlobalVarsCollector::Finish \"" << parentItem.Print() << "\", old globals: "
        << FormatURV(parentVars.GetUsedReservedVars())
        << Endl;
    YDIAG(UIDs)
        << "TGlobalVarsCollector::Finish \"" << parentItem.Print() << "\", locals: "
        << FormatURV(urvLocal)
        << Endl;

    auto& urvGlobal = parentVars.GetUsedReservedVars();
    if (urvLocal.FromVars) {
        TUsedReservedVars::Expand(GetOrInit(urvGlobal.FromVars), *urvLocal.FromVars);
    }
    if (urvLocal.FromCmd) {
        Y_ASSERT(!urvGlobal.FromCmd);
        GetOrInit(urvGlobal.FromCmd) = *urvLocal.FromCmd;
    }
    if (urvGlobal.FromCmd && urvGlobal.FromVars) {
        TUsedReservedVars::Expand(*urvGlobal.FromCmd, *urvGlobal.FromVars);
    }
    if (urvGlobal.FromCmd) {
        Y_ASSERT(!urvTotal);
        GetOrInit(urvTotal) = *urvGlobal.FromCmd;
    }

    YDIAG(UIDs)
        << "TGlobalVarsCollector::Finish \"" << parentItem.Print() << "\", new globals: "
        << FormatURV(parentVars.GetUsedReservedVars())
        << Endl;
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
    const auto& peerVars = RestoreContext.Modules.GetGlobalVars(peer->GetId());
    auto& parentVars = RestoreContext.Modules.GetGlobalVars(parentItem.Node()->ElemId);
    for (const auto& peerVar : peerVars.GetVars()) {
        parentVars.GetVars()[peerVar.first].AppendUnique(peerVar.second);
    }
    YDIAG(UIDs)
        << "TGlobalVarsCollector::Collect \"" << parentItem.Print() << "\" from " << RestoreContext.Graph.ToString(peerNode)
        << Endl;
    YDIAG(UIDs)
        << "TGlobalVarsCollector::Collect \"" << parentItem.Print() << "\", old globals: "
        << FormatURV(parentVars.GetUsedReservedVars())
        << Endl;
    YDIAG(UIDs)
        << "TGlobalVarsCollector::Collect \"" << parentItem.Print() << "\", peer globals: "
        << FormatURV(peerVars.GetUsedReservedVars())
        << Endl;
    if (peerVars.GetUsedReservedVars().FromVars)
        TUsedReservedVars::Expand(GetOrInit(parentVars.GetUsedReservedVars().FromVars), *peerVars.GetUsedReservedVars().FromVars);
    YDIAG(UIDs)
        << "TGlobalVarsCollector::Collect \"" << parentItem.Print() << "\", new globals: "
        << FormatURV(parentVars.GetUsedReservedVars())
        << Endl;
}
