#pragma once

#include "macro.h"
#include "macro_processor.h"
#include "vars.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/command_store.h>

#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

class TBuildConfiguration;
class TYMake;
struct TRestoreContext;
class TModules;
struct TDumpInfoEx;

class TMakeModuleState;
class TMakeModuleStates;

using TMakeModuleStatePtr = TSimpleIntrusivePtr<TMakeModuleState>;

class TMakeCommand {
public:
    TVars Vars;
    TCommandInfo CmdInfo; // here we get correct coordinates for input, output and tool subst
    TVector<std::span<TVarStr>> Inputs;

private:
    TMakeModuleStatePtr ModuleState;

    TVars BaseVars;

    const TCommands* Commands;

    const TBuildConfiguration& Conf;
    TModules& Modules;
    TDepGraph& Graph;
    TMakeModuleStates& ModulesStatesCache;

    // set by MineInputsAndOutputs
    TString MainFileName;
    TString CmdString; // unexpanded macro string
    TNodeId CmdNode;

    // set by InitModuleEnv
    bool RequirePeers = false;

public:
    explicit TMakeCommand(TMakeModuleStates& modulesStatesCache, TYMake& yMake);
    TMakeCommand(TMakeModuleStates& modulesStatesCache, TYMake& yMake, const TVars* base0);

    explicit TMakeCommand(
        TMakeModuleStates& modulesStatesCache,
        const TRestoreContext& restoreContext,
        const TCommands& commands,
        TUpdIter* updIter = nullptr,
        const TVars* base0 = nullptr);

    void GetFromGraph(TNodeId nodeId, TNodeId modId, ECmdFormat cmdFormat, TDumpInfoEx* addinfo = nullptr, bool skipRender = false, bool isGlobalNode = false);
    void RenderCmdStr(ECmdFormat cmdFormat, TErrorShowerState* errorShower);

    static void ReportStats();

private:
    void InitModuleEnv(TNodeId modId);
    void MineInputsAndOutputs(TNodeId nodeId, TNodeId modId);
    void MineVarsAndExtras(TDumpInfoEx* addInfo, TNodeId nodeId, TNodeId modId);
    void MineLateOuts(TDumpInfoEx* addInfo, const TUniqVector<TNodeId>& lateOutsProps, TNodeId nodeId, TNodeId modId);
    bool IsFakeModule(TDepTreeNode nodeVal);

    TString RealPath(const TConstDepNodeRef& node) const;
    TString RealPathEx(const TConstDepNodeRef& node) const;

    static inline NStats::TMakeCommandStats& GetStats();
};

class TMakeModuleState : public TSimpleRefCount<TMakeModuleState> {
public:
    TMakeModuleState(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules, TNodeId moduleId);

    TFileView CurDir;
    TVars Vars;
    THashSet<TNodeId> PeerIds;
    const TUniqVector<TNodeId>* GlobalSrcs;
};

class TMakeModuleStates {
private:
    const TBuildConfiguration& Conf_;
    TDepGraph& Graph_;
    TModules& Modules_;

    TNodeId LastStateId_ = TNodeId::Invalid;
    TMakeModuleStatePtr LastState_;

public:
    TMakeModuleStates(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules)
        : Conf_(conf), Graph_(graph), Modules_(modules)
    {
    }

    TMakeModuleStatePtr GetState(TNodeId moduleId);

    static inline NStats::TMakeCommandStats& GetStats();
};
