#include "mkcmd.h"

#include "conf.h"
#include "dump_info.h"
#include "module_restorer.h"
#include "macro.h"
#include "macro_processor.h"
#include "mine_variables.h"
#include "prop_names.h"
#include "vars.h"
#include "ymake.h"

#include <devtools/ymake/json_subst.h>
#include <devtools/ymake/mkcmd_inputs_outputs.h>

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>

#include <devtools/ymake/options/static_options.h>

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/common/npath.h>

#include <util/folder/path.h>
#include <util/generic/hash_set.h>
#include <util/generic/singleton.h>
#include <util/generic/string.h>
#include <util/generic/utility.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

TMakeModuleState::TMakeModuleState(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules, TNodeId moduleId) {
    const auto modNode = graph[moduleId];
    auto& fileConf = graph.Names().FileConf;
    TFileView modName = graph.GetFileName(modNode);

    CurDir = fileConf.ReplaceRoot(fileConf.Parent(modName), NPath::Source);

    Vars.SetPathResolvedValue(NVariableDefs::VAR_TARGET, conf.RealPath(modName));
    Vars.SetPathResolvedValue(NVariableDefs::VAR_BINDIR, conf.RealPath(fileConf.ReplaceRoot(CurDir, NPath::Build)));
    Vars.SetPathResolvedValue(NVariableDefs::VAR_CURDIR, conf.RealPath(CurDir));
    Vars.SetValue(NVariableDefs::VAR_ARCADIA_BUILD_ROOT, conf.BuildRoot.c_str());
    Vars.SetValue(NVariableDefs::VAR_ARCADIA_ROOT, conf.SourceRoot.c_str());

    Vars[NVariableDefs::VAR_SRCS_GLOBAL];
    Vars[NVariableDefs::VAR_AUTO_INPUT];
    Vars[NVariableDefs::VAR_PEERS];

    for (const auto* names : {&conf.ReservedNames, &conf.ResourceNames})  {
        for (const TString& name : *names) {
            Vars[name];
        }
    }

    bool moduleUsesPeers = modules.Get(modNode->ElemId)->GetAttrs().UsePeers;

    TModuleRestorer restorer({conf, graph, modules}, modNode);
    restorer.RestoreModule();
    restorer.UpdateLocalVarsFromModule(Vars, conf, moduleUsesPeers);
    restorer.UpdateGlobalVarsFromModule(Vars);
    restorer.GetModuleDepIds(PeerIds);
    GlobalSrcs = &restorer.GetGlobalSrcsIds();
}

TMakeModuleStatePtr TMakeModuleStates::GetState(TNodeId moduleId) {
    GetStats().Inc(NStats::EMakeCommandStats::InitModuleEnvCalls);
    if (LastStateId_ != moduleId) {
        LastStateId_ = moduleId;
        LastState_.Reset(new TMakeModuleState{Conf_, Graph_, Modules_, moduleId});
        GetStats().Inc(NStats::EMakeCommandStats::InitModuleEnv);

    }
    return LastState_;
}

inline NStats::TMakeCommandStats& TMakeModuleStates::GetStats() {
    static NStats::TMakeCommandStats stats{"TMakeCommand stats"};
    return stats;
}

TMakeCommand::TMakeCommand(TMakeModuleStates& modulesStatesCache, TYMake& yMake)
    : TMakeCommand(modulesStatesCache, yMake, nullptr)
{
}

TMakeCommand::TMakeCommand(TMakeModuleStates& modulesStatesCache, TYMake& yMake, const TVars* base0)
    : TMakeCommand{modulesStatesCache, yMake.GetRestoreContext(), yMake.Commands, yMake.UpdIter, base0}
{
}

TMakeCommand::TMakeCommand(TMakeModuleStates& modulesStatesCache, const TRestoreContext& restoreContext, const TCommands& commands, TUpdIter* updIter, const TVars* base0)
    : Vars(&BaseVars) // hack for SET_APPEND
    , CmdInfo(restoreContext.Conf, &restoreContext.Graph, updIter)
    , BaseVars(base0)
    , Commands(&commands)
    , Conf(restoreContext.Conf)
    , Modules(restoreContext.Modules)
    , Graph(restoreContext.Graph)
    , ModulesStatesCache(modulesStatesCache)
{
}

TString TMakeCommand::RealPath(const TConstDepNodeRef& node) const {
    return ::RealPath(Conf, Graph, node);
}
TString TMakeCommand::RealPathEx(const TConstDepNodeRef& node) const {
    const auto& fileConf = Graph.Names().FileConf;
    TFileView resolvedNode = fileConf.ResolveLink(Graph.GetFileName(node));
    return Conf.RealPathEx(resolvedNode);
}

void TMakeCommand::GetFromGraph(TNodeId nodeId, TNodeId modId, ECmdFormat cmdFormat, TDumpInfoEx* addInfo, bool skipRender, bool isGlobalNode) {
    InitModuleEnv(modId);
    RequirePeers = (nodeId == modId) && Modules.Get(Graph[modId]->ElemId)->GetAttrs().UsePeers;
    if (isGlobalNode) {
        Vars.SetPathResolvedValue(NVariableDefs::VAR_GLOBAL_TARGET, Conf.RealPath(Graph.GetFileName(Graph.Get(nodeId))));
    }
    if (nodeId != TNodeId::Invalid) {
        MineInputsAndOutputs(nodeId, modId);
        MineVarsAndExtras(addInfo, nodeId, modId);
        CmdInfo.KeepTargetPlatform = Graph.Names().CommandConf.GetById(TVersionedCmdId(Graph[CmdNode]->ElemId).CmdId()).KeepTargetPlatform;
        if (!skipRender) {
            auto ignoreErrors = TErrorShowerState(TDebugOptions::EShowExpressionErrors::None);
            RenderCmdStr(cmdFormat, &ignoreErrors);
        }
    }
}

bool TMakeCommand::IsFakeModule(TDepTreeNode nodeVal) {
    if (IsModuleType(nodeVal.NodeType)) {
        return Modules.Get(nodeVal.ElemId)->IsFakeModule();
    }
    return false;
}

void TMakeCommand::MineInputsAndOutputs(TNodeId nodeId, TNodeId modId) {
    Y_ASSERT(ModuleState);

    const auto node = Graph[nodeId];
    Y_ASSERT(UseFileId(node->NodeType)); //
    auto mainFileView = Graph.GetFileName(node);
    MainFileName = mainFileView.GetTargetStr();
    YDIAG(MkCmd) << "Build: " << MainFileName << Endl;
    bool isModule = nodeId == modId;
    TVarStrEx mainOut = TVarStrEx(RealPathEx(node), node->ElemId, true);

    TVector<TVarStrEx> output;
    if (mainFileView.GetLinkType() != ELT_Action)
        output.push_back(mainOut);

    if (isModule) {
        // For module TARGET is always a first output.
        CmdInfo.AddOutputInternal(mainOut);
    }

    bool cmdfound = false;

    TStringBuf moduleDir;
    auto getModuleDir = [&]() {
        if (moduleDir.empty()) {
            moduleDir = ModuleState->CurDir.GetTargetStr();
        }
        return moduleDir;
    };

    auto isGlobalSrc = [&](const TConstDepNodeRef& depNode) -> bool {
        if (isModule && ModuleState->GlobalSrcs) {
            return ModuleState->GlobalSrcs->has(depNode.Id());
        }

        return false;
    };

    auto addInput = [&](const TConstDepNodeRef& node, bool explicitInputs){
        YDIAG(MkCmd) << "Input dependency: " << MainFileName << " " << Graph.ToString(node) << Endl;

        TString inputPath = InputToPath(Conf, node, getModuleDir);
        TVarStr inputVarItem = TVarStr(inputPath, false, true);

        if (!explicitInputs) {
            // We don't need GlobalSrcs in AUTO_INPUT.
            Y_ASSERT(!isGlobalSrc(node));

            inputVarItem.IsAuto = true;
            Vars[NVariableDefs::VAR_AUTO_INPUT].push_back(inputVarItem);

        } else {
            Vars[NVariableDefs::VAR_INPUT].push_back(inputVarItem);
        }
    };

    auto addSpanInput = [&](ui32 inputId, ui32 inputSize) {
        Inputs.push_back({ Vars[NVariableDefs::VAR_INPUT].begin() + inputId, inputSize });
    };

    auto addOutput = [&](const TConstDepNodeRef& depNode) {
        output.push_back(TVarStrEx(RealPathEx(depNode), depNode->ElemId, true));
    };

    auto setCmd = [&](const TConstDepNodeRef& depNode) {
        CmdString = Graph.GetCmdName(depNode).GetStr();
        CmdNode = depNode.Id();
        cmdfound = true;
    };


    ProcessInputsAndOutputs<false>(node, isModule, Modules, addInput, isGlobalSrc, addSpanInput, addOutput, setCmd);

    if (!cmdfound) {
        throw TMakeError() << "No pattern for node " << MainFileName;
    }

    // TODO: INPUT, OUTPUT has to be released as internal variables.
    // Module as target shouldn't be in OUTPUT.
    Vars[NVariableDefs::VAR_OUTPUT].Assign(output);
}

void TMakeCommand::InitModuleEnv(TNodeId modId) {
    ModuleState = ModulesStatesCache.GetState(modId);

    for (auto& [name, var] : ModuleState->Vars) {
        Vars[name] = var;
    }
}

void TMakeCommand::MineVarsAndExtras(TDumpInfoEx* addInfo, TNodeId nodeId, TNodeId modId) {
    TYVar& inputVar = Vars[NVariableDefs::VAR_INPUT];

    for (const auto& input : inputVar) {
        // CanonPath can emit a configure event
        Conf.CanonPath(input.Name);
    }

    if (addInfo) {
        addInfo->SetExtraValues(Vars); // f.e. it sets $UID in Vars in TSubst2Json

        Y_ASSERT(ModuleState);
        if (RequirePeers && !ModuleState->PeerIds.empty()) {
            TVector<std::pair<TStringBuf, TNodeId>> peers;
            for (TNodeId peerId : ModuleState->PeerIds) {
                const auto node = Graph[peerId].Value();
                if (!IsFakeModule(node)) {
                    peers.push_back({Graph.GetFileName(node).GetTargetStr(), peerId});
                }
            }
            Sort(peers);
            for (const auto& [_, peerId] : peers) {
                addInfo->Deps.Push(peerId);
            }
        }
    }

    TUniqVector<TNodeId> lateOutsProps;
    MineVariables(Conf, Graph[CmdNode], CmdInfo.ToolPaths, CmdInfo.ResultPaths, Vars, lateOutsProps, Modules);
    if (Graph.GetCmdName(Graph[CmdNode]).IsNewFormat())
        MineVariables(Graph[nodeId], Vars);
    MineLateOuts(addInfo, lateOutsProps, nodeId, modId);
}

void TMakeCommand::MineLateOuts(TDumpInfoEx* addInfo, const TUniqVector<TNodeId>& lateOutsProps, TNodeId nodeId, TNodeId modId) {
    if (!addInfo) {
        return;
    }
    bool isModule = nodeId == modId;
    ui32 nodeElemId = Graph.Get(nodeId)->ElemId;
    TVector<TString> cmdLateOuts;
    if (isModule) {
        Modules.ClearModuleLateOuts(nodeElemId);
    }
    auto& lateOuts = isModule ? Modules.GetModuleLateOuts(nodeElemId) : cmdLateOuts;
    TCommandInfo::TSubstObserver lateOutsObserver = [&](const TVarStr& subst) {
        if (!subst.Name.StartsWith(Conf.BuildRoot.c_str())) {
            YConfErr(Misconfiguration) << "Late output must be full arcadia path strarting from ${ARCADIA_BUILD_ROOT} or ${BINDIR} " << subst.Name << Endl;
            return;
        }
        Y_ASSERT(addInfo);
        lateOuts.push_back(subst.Name);
    };

    auto isNewFormat = Graph.GetCmdName(Graph[CmdNode]).IsNewFormat();

    for (const TNodeId lateOutNodeId : lateOutsProps) {
        TString dummy;
        const auto pattern = GetPropertyValue(Graph.GetCmdName(Graph.Get(lateOutNodeId)).GetStr());
        if (isNewFormat) {
            Y_DEBUG_ABORT_UNLESS(Commands);
            if (Y_LIKELY(Commands)) {
                auto& cmdSrc = *Commands;
                auto& conf = Graph.Names().CommandConf;
                auto& expr = *cmdSrc.Get(pattern, &conf);
                auto dummyCmdInfo = TCommandInfo(Conf, nullptr, nullptr);
                auto scr = TCommands::SimpleCommandSequenceWriter()
                    .Write(cmdSrc, expr, Vars, {}, dummyCmdInfo, &conf, Conf)
                    .Extract();
                for (auto& cmd : scr)
                    for (auto& arg : cmd)
                        lateOutsObserver(arg);
            }
        } else {
            TVector<TMacroData> macros;
            GetMacrosFromPattern(pattern, macros, false);
            for (TMacroData& macroData: macros) {
                macroData.Flags.Reset(EMF_LateOut);
                macroData.Flags.Reset(EMF_Hide);
                dummy.clear();
                CmdInfo.SubstData(nullptr, macroData, Vars, ECF_ExpandVars, ESM_DoSubst, dummy, ECF_Unset, lateOutsObserver);
            }
        }
    }

    addInfo->LateOuts.reserve(addInfo->LateOuts.size() + lateOuts.size());
    addInfo->LateOuts.insert(addInfo->LateOuts.end(), lateOuts.begin(), lateOuts.end());
}

void TMakeCommand::RenderCmdStr(ECmdFormat cmdFormat, TErrorShowerState* errorShower) {
    //recursively add command parts!
    if (Graph.Names().CmdNameById(Graph[CmdNode]->ElemId).IsNewFormat()) {
        auto expr = Commands->GetByElemId(Graph[CmdNode]->ElemId);
        YDIAG(MkCmd) << "CS for: " << Commands->PrintCmd(*expr) << "\n";
        if (!CmdInfo.MkCmdAcceptor) {
            // TBD: we can get here, e.g., from the MSVS generator; what's the goal?
            return;
        }
        auto acceptor = CmdInfo.MkCmdAcceptor->Upgrade();
        Y_ABORT_UNLESS(acceptor);
        Commands->WriteShellCmd(acceptor, *expr, Vars, Inputs, CmdInfo, &Graph.Names().CommandConf, Conf, errorShower);
        acceptor->PostScript(Vars);
    } else {
        YDIAG(MkCmd) << "CS for: " << CmdString << "\n";
        CmdInfo.SetCommandSource(Commands);
        CmdString = CmdInfo.SubstMacro(nullptr, CmdString, ESM_DoSubst, Vars, ECF_ExpandSimpleVars, true);
        CmdInfo.SubstMacro(nullptr, CmdString, ESM_DoSubst, Vars, cmdFormat, false);
        const auto& cmdInfo = CmdInfo;

        TYVar& toolVar = Vars[NVariableDefs::VAR_TOOLS];
        if (cmdInfo.ToolPaths) {
            for (const auto& tool : *cmdInfo.ToolPaths) {
                toolVar.push_back(TVarStr(tool.second, true, false));
            }
        }
    }
}

void TMakeCommand::ReportStats() {
    GetStats().Report();
}

inline NStats::TMakeCommandStats& TMakeCommand::GetStats() {
    return TMakeModuleStates::GetStats();
}
