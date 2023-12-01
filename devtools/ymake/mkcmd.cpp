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

class TMakeModuleState : public TSimpleRefCount<TMakeModuleState> {
public:
    TMakeModuleState(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules, ui32 moduleId) {
        const auto modNode = graph[moduleId];
        auto& fileConf = graph.Names().FileConf;
        TFileView modName = graph.GetFileName(modNode);

        CurDir = fileConf.ReplaceRoot(fileConf.Parent(modName), NPath::Source);

        Vars.SetPathResolvedValue("TARGET", conf.RealPath(modName));
        Vars.SetPathResolvedValue("BINDIR", conf.RealPath(fileConf.ReplaceRoot(CurDir, NPath::Build)));
        Vars.SetPathResolvedValue("CURDIR", conf.RealPath(CurDir));
        Vars.SetValue("ARCADIA_BUILD_ROOT", conf.BuildRoot.c_str());
        Vars.SetValue("ARCADIA_ROOT", conf.SourceRoot.c_str());

        Vars["SRCS_GLOBAL"];
        Vars["AUTO_INPUT"];
        Vars["PEERS"];

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

public:
    TFileView CurDir;

    TVars Vars;

    THashSet<TNodeId> PeerIds;
    const TUniqVector<TNodeId>* GlobalSrcs;
};

class TMakeModuleStates {
private:
    const TBuildConfiguration& Conf;
    TDepGraph& Graph;
    TModules& Modules;

    ui32 LastStateId_ = 0;
    TMakeModuleStatePtr LastState_;

    friend TMakeModuleStates& GetMakeModulesStates(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules);

public:
    TMakeModuleStates(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules)
        : Conf(conf), Graph(graph), Modules(modules)
    {
    }

    TMakeModuleStatePtr GetState(ui32 moduleId) {
        GetStats().Inc(NStats::EMakeCommandStats::InitModuleEnvCalls);
        if (LastStateId_ != moduleId) {
            LastStateId_ = moduleId;
            LastState_.Reset(new TMakeModuleState{Conf, Graph, Modules, moduleId});
            GetStats().Inc(NStats::EMakeCommandStats::InitModuleEnv);

        }
        return LastState_;
    }

    static inline NStats::TMakeCommandStats& GetStats();
};

TMakeModuleStates& GetMakeModulesStates(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules) {
    TMakeModuleStates& states = *Singleton<TMakeModuleStates>(conf, graph, modules);
    Y_ASSERT(&states.Conf == &conf);
    Y_ASSERT(&states.Graph == &graph);
    Y_ASSERT(&states.Modules == &modules);
    return states;
}

inline NStats::TMakeCommandStats& TMakeModuleStates::GetStats() {
    static NStats::TMakeCommandStats stats{"TMakeCommand stats"};
    return stats;
}

TMakeCommand::TMakeCommand(TYMake& yMake)
    : TMakeCommand(yMake, nullptr)
{
}

TMakeCommand::TMakeCommand(TYMake& yMake, const TVars* base0)
    : TMakeCommand{yMake.GetRestoreContext(), yMake.Commands, yMake.UpdIter, base0}
{
}

TMakeCommand::TMakeCommand(const TRestoreContext& restoreContext, const TCommands& commands, TUpdIter* updIter, const TVars* base0)
    : Vars(&BaseVars) // hack for SET_APPEND
    , CmdInfo(&restoreContext.Conf, &restoreContext.Graph, updIter)
    , BaseVars(base0)
    , Commands(&commands)
    , Conf(restoreContext.Conf)
    , Modules(restoreContext.Modules)
    , Graph(restoreContext.Graph)
{
}

TString TMakeCommand::RealPath(const TConstDepNodeRef& node) const {
    const auto& fileConf = Graph.Names().FileConf;
    TFileView resolvedNode = fileConf.ResolveLink(Graph.GetFileName(node));
    return Conf.RealPath(resolvedNode);
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
        Vars.SetPathResolvedValue("GLOBAL_TARGET", Conf.RealPath(Graph.GetFileName(Graph.Get(nodeId))));
    }
    if (nodeId != 0) {
        MineInputsAndOutputs(nodeId, modId);
        MineVarsAndExtras(addInfo, nodeId, modId);
        CmdInfo.KeepTargetPlatform = Graph.Names().CommandConf.GetById(TVersionedCmdId(Graph[CmdNode]->ElemId).CmdId()).KeepTargetPlatform;
        if (!skipRender) {
            RenderCmdStr(cmdFormat);
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
    MainFileName = Graph.GetFileName(node).GetTargetStr();
    YDIAG(MkCmd) << "Build: " << MainFileName << Endl;
    bool isModule = nodeId == modId;
    TVarStrEx mainOut = TVarStrEx(RealPathEx(node), node->ElemId, true);

    TVector<TVarStrEx> output;
    output.push_back(mainOut);
    if (isModule) {
        CmdInfo.AddOutputInternal(mainOut); //for module TARGET is always a first output
    }

    size_t numDeps = node.Edges().Total();
    //UNPACK_64 -> in NumDeps=quantity of Deps; Ptr to first dep
    bool cmdfound = false;
    bool explicitInputs = true;
    size_t cnt = 0;

    auto addInput = [&](const TConstDepRef& dep){
        const TNodeId node = dep.To().Id();
        YDIAG(MkCmd) << "Input dependency: " << MainFileName << " " << (int)(*dep) << Endl;
        TString inpDep = RealPath(dep.To());
        if (inpDep.empty()) {
            ///if input file is $U - best effort to resolve it in CurDir
            auto& fileConf = Graph.Names().FileConf;
            const auto depName = fileConf.ResolveLink(Graph.GetFileName(dep.To()));
            YErr() << depName << ": resolve this input file in current source dir " << ModuleState->CurDir << ". Be ready for build problems." << Endl;
            inpDep = Conf.RealPath(NPath::Join(ModuleState->CurDir.GetTargetStr(), depName.CutType()));
        }
        TVarStr inpFile = TVarStr(inpDep, false, true);
        if (!explicitInputs) {
            Y_ASSERT(ModuleState->GlobalSrcs && !ModuleState->GlobalSrcs->has(node)); //we don't need global Srcs in AUTO_INPUT
            inpFile.IsAuto = true;
            Vars["AUTO_INPUT"].push_back(inpFile);
        } else {
            Vars["INPUT"].push_back(inpFile);
        }
    };

    for (const auto dep : node.Edges()) {
        const auto depNode = dep.To();
        const auto depNodeVal = *dep.To();
        const auto depType = *dep;
        YDIAG(MkCmd) << cnt++ << " dep from " << numDeps << ": " << Graph.ToString(depNode) << Endl;

        if (isModule && IsModuleType(depNodeVal.NodeType)) {
            continue;
        }

        if (IsIndirectSrcDep(dep)) {
            Y_ASSERT(explicitInputs);
            for (const auto& depdep: dep.To().Edges()) {
                if (*depdep == EDT_Property && depdep.To()->NodeType == EMNT_File) {
                    addInput(depdep);
                }
            }
        } else if (depNodeVal.NodeType == EMNT_BuildCommand && depType == EDT_BuildCommand) {
            TStringBuf depName = Graph.GetCmdName(depNode).GetStr();
            CmdString = depName;
            CmdNode = depNode.Id();
            cmdfound = true;
        } else if (depType == EDT_OutTogetherBack) {
            if (!IsFakeModule(depNodeVal)) {
                output.push_back(TVarStrEx(RealPathEx(depNode), depNodeVal.ElemId, true)); //move "OUTPUT" and "INPUT" to const?
            }
        } else if (depType == EDT_Group) {
            TStringBuf depName = Graph.GetCmdName(depNode).GetStr();
            explicitInputs = depName == NStaticConf::INPUTS_MARKER;
        } else if (depType == EDT_BuildFrom && UseFileId(depNodeVal.NodeType) && !IsFakeModule(depNodeVal) && (!isModule || ModuleState->GlobalSrcs && !ModuleState->GlobalSrcs->has(depNode.Id()))) {
            if (!IsDirType(depNodeVal.NodeType) || !isModule) {
                addInput(dep);
            }
        }
    }
    if (!cmdfound) {
        throw TMakeError() << "No pattern for node " << MainFileName;
    }

    //TODO: INPUT, OUTPUT has to be released as internal variables.
    // Module as target shouldn't be in OUTPUT
    Vars["OUTPUT"].Assign(output);
}

void TMakeCommand::InitModuleEnv(TNodeId modId) {
    ModuleState = GetMakeModulesStates(Conf, Graph, Modules).GetState(modId);

    for (auto& [name, var] : ModuleState->Vars) {
        Vars[name] = var;
    }
}

void TMakeCommand::MineVarsAndExtras(TDumpInfoEx* addInfo, TNodeId nodeId, TNodeId modId) {
    TYVar& inputVar = Vars["INPUT"];

    for (const auto& input : inputVar) {
        // CanonPath can emit a configure event
        Conf.CanonPath(input.Name);
    }

    if (addInfo) {
        addInfo->SetExtraValues(Vars); // f.e. it sets $UID in Vars in TSubst2Json

        THashSet<TStringBuf> originalExtraInput;
        TYVar& extraInput = addInfo->ExtraInput;
        extraInput.InsertNamesTo(originalExtraInput);

        for (const auto& input : inputVar) {
            if (!originalExtraInput.contains(input.Name)) {
                extraInput.push_back(input);
            }
        }

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

    for (const TNodeId lateOutNodeId : lateOutsProps) {
        TString dummy;
        const auto pattern = GetPropertyValue(Graph.GetCmdName(Graph.Get(lateOutNodeId)).GetStr());
        TVector<TMacroData> macros;
        GetMacrosFromPattern(pattern, macros, false);
        for (TMacroData& macroData: macros) {
            macroData.Flags.Reset(EMF_LateOut);
            macroData.Flags.Reset(EMF_Hide);
            dummy.clear();
            CmdInfo.SubstData(nullptr, macroData, Vars, ECF_ExpandVars, ESM_DoSubst, dummy, ECF_Unset, lateOutsObserver);
        }
    }

    addInfo->LateOuts.reserve(addInfo->LateOuts.size() + lateOuts.size());
    addInfo->LateOuts.insert(addInfo->LateOuts.end(), lateOuts.begin(), lateOuts.end());
}

void TMakeCommand::RenderCmdStr(ECmdFormat cmdFormat) {
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
        Commands->WriteShellCmd(acceptor, *expr, Vars, CmdInfo);
        acceptor->PostScript(Vars);
    } else {
        YDIAG(MkCmd) << "CS for: " << CmdString << "\n";
        CmdString = CmdInfo.SubstMacro(nullptr, CmdString, ESM_DoSubst, Vars, ECF_ExpandSimpleVars, true);
        CmdInfo.SubstMacro(nullptr, CmdString, ESM_DoSubst, Vars, cmdFormat, false);
        const auto& cmdInfo = CmdInfo;

        if (Vars["INPUT"].size() == cmdInfo.GetInput().size()) {
            for (size_t n = 0; n < Vars["INPUT"].size(); n++) {
                Vars["INPUT"][n].MsvsSource = cmdInfo.GetInput()[n].MsvsSource;
            }
        } else {
            const auto* mainOutput = cmdInfo.GetMainOutput();
            YDIAG(Dev) << "Input sequence mismatch: " << (mainOutput ? mainOutput->Name : "") << Endl;
        }

        TYVar& toolVar = Vars["TOOLS"];
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
