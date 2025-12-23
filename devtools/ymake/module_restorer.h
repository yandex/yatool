#pragma once

#include "conf.h"

#include <devtools/ymake/compact_graph/dep_graph.h>

class TModule;
class TModules;
class TBuildConfiguration;

struct TRestoreContext {
    const TBuildConfiguration& Conf;
    TDepGraph& Graph;
    TModules& Modules;

    TRestoreContext(const TBuildConfiguration& conf, TDepGraph& graph, TModules& modules)
        : Conf(conf)
        , Graph(graph)
        , Modules(modules)
    {
    }

    TRestoreContext(const TRestoreContext&) = default;
};

class TModuleRestorer {
public:
    TModuleRestorer(TRestoreContext context, const TConstDepNodeRef& node)
        : Context(context)
        , Node(node)
        , Module(nullptr)
    {
    }

    /// Call module initialization and mining local vars
    TModule* RestoreModule();

    /// Append module variables (INCLUDE, PEERS, GLOBAL_SRCS) to vars
    void UpdateLocalVarsFromModule(TVars& vars, const TBuildConfiguration& conf, bool moduleUsesPeers);

    /// Call mining module global variables (ends with "_RESOURCE_GLOBAL" and reserved names)
    /// and append them to vars
    void UpdateGlobalVarsFromModule(TVars& vars);

    /// Return set of NodeIds for dependencies (peer modules and global srcs commands)
    void GetModuleDepIds(THashSet<TNodeId>& ids);

    /// Return transitive closure of module peerdirs.
    const TUniqVector<TNodeId>& GetPeers();

    /// Return transitive closure of module tools.
    const TUniqVector<TNodeId>& GetTools();

    /// Transform collection of peers ElemIds passed recursively to peerdir TNodeIds
    void GetPeerDirIds(THashSet<TNodeId>& peerDirIds);

    /// Return collection of global srcs TNodeIds collected recursively by PEERDIRs
    const TUniqVector<TNodeId>& GetGlobalSrcsIds();

    TModule* GetModule();

    const TModule* GetModule() const;

    void MineModuleDirs();

    /// This mines:
    /// - module vars (INCLUDE, PEERS, GLOBAL_SRCS),
    /// - collections of TNodeId for passed modules and directories
    /// as well as MineIncludesRecursive
    void MinePeers();

    /// Mines values of variables created with _GLOB and _LATE_GLOB macro. Uses RealPath internally and must
    /// be called only when TBuildConfiguration::RealPath is prepared to generate executor file paths.
    void MineGlobVars();

    /// This mines module global vars (ends with "_RESOURCE_GLOBAL" and reserved names)
    /// as well as MineIncludesRecursive
    void MineGlobalVars();

private:
    TVarStr& AddPath(TYVar& var, const TStringBuf& what);

    bool IsFakeModule(ui32 elemId) const;

private:
    TRestoreContext Context;
    const TConstDepNodeRef Node;
    TModule* Module;
};

struct TTarget;

TVector<TConstDepNodeRef> GetStartModules(const TDepGraph& graph, const THashSet<TNodeId>& startDirs);
TVector<TConstDepNodeRef> GetStartModules(const TDepGraph& graph, const TVector<TTarget>& startTargets);
TVector<TTarget> GetStartTargetsModules(const TDepGraph& graph, const TVector<TTarget>& startTargets);
TVector<TDepNodeRef> GetStartModules(TDepGraph& graph, const TVector<TTarget>& startTargets);

TModule& InitModule(TModules& modules, const TVars& commandConf, const TConstDepNodeRef& node);
