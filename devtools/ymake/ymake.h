#pragma once

#include "add_iter.h"
#include "command_store.h"
#include "parser_manager.h"
#include "general_parser.h"
#include "module_store.h"
#include "module_restorer.h"  // for TRestoreContext
#include "module_resolver.h"  // for TModuleResolveContext
#include "saveload.h"

#include <devtools/ymake/symbols/time_store.h>

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_starts_ctx.h>

#include <devtools/ymake/resolver/resolve_ctx.h>

#include <util/generic/hash_set.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

#include <util/stream/format.h>

using TDependsToModulesClosure = THashMap<TString, TVector<TNodeId>>;

class TNodeEdgesComparator {
private:
    EMakeNodeType FromType;
    const TDepGraph& Graph;

public:
    TNodeEdgesComparator(TConstDepNodeRef node);

    bool operator() (const TDepGraph::TEdge& edge1, const TDepGraph::TEdge& edge2) const noexcept;
};

class TYMake {
public:
    static const ui64 ImageVersion;
    TBuildConfiguration& Conf;

    TDepGraph Graph;       // graph itself
    TDepGraph RecurseGraph;       // recurse graph
    TSymbols Names;        // two hashes for all aims: Id2Names + Names2Ids
    TTimeStamps TimeStamps; // object for keeping time scan times in just 1 byte
    TGeneralParser* Parser = nullptr; // functions for processing of files|dirs
    TUpdIter* UpdIter = nullptr;    // depth-first iterator to add or update nodes of the graph
    TIncParserManager IncParserManager;

    //what we actually requested to build from command line

    TVector<TTarget> StartTargets;
    TVector<TTarget> RecurseStartTargets;
    THashSet<TTarget> ModuleStartTargets;
    bool HasNonDirTargets = false;

    TDependsToModulesClosure DependsToModulesClosure;
    THashMap<ui32, TVector<TNodeId>> ExtraTestDeps; // "dirId -> test classpath" TODO(svidyuk) remove me after jbuild death

    NYndex::TYndex Yndex;

    TModules Modules;
    TCommands Commands;

private:
    TFsPath DepCacheTempFile;      // Name of temporary file with delayed save data
    TFsPath UidsCacheTempFile;      // Name of temporary file with delayed save data
    TString PrevDepsFingerprint;
    TString CurrDepsFingerprint;

    TVector<ui32> PreserveStartTargets() const;
    void FixStartTargets(const TVector<ui32>& elemIds);
    bool TryLoadUids(TUidsCachable* uidsCachable);

public:
    explicit TYMake(TBuildConfiguration& conf);
    void PostInit(); // Call this after Load: this may rely on loaded symbol table
    ~TYMake();

    // Returns true if directory loops found
    bool DumpLoops();
    void BuildDepGraph();
    void CreateRecurseGraph();
    bool InitTargets();
    void AddRecursesToStartTargets();
    void AddModulesToStartTargets();
    void AddPackageOutputs();
    void SortAllEdges();
    void DumpDependentDirs(IOutputStream& cmsg, bool skipDepends = false);
    void DumpSrcDeps(IOutputStream& cmsg);
    void PrintTargetDeps(IOutputStream& cmsg);
    void DumpBuildTargets(IOutputStream& cmsg);
    void DumpTestDart(IOutputStream& cmsg);
    void DumpJavaDart(IOutputStream& cmsg);
    void DumpMakeFilesDart(IOutputStream& cmsg);
    void ReportConfigureEvents();
    void ReportGraphBuildStats();
    void ReportModulesStats();
    void ReportMakeCommandStats();
    void ReportDepsToIsolatedProjects();
    void FindLostIncludes();

    void ListTargetResults(const TTarget& startTarget, TVector<TNodeId>& dirMods, TVector<TNodeId>& globSrcs) const;
    bool ResolveRelationTargets(const TVector<TString>& targets, THashSet<TNodeId>& result);

    // Debug functions
    void DumpGraph();
    void FindPathBetween(const TVector<TString>& from,
                         const TVector<TString>& to);
    void FindMissingPeerdirs();
    void AssignSrcsToModules(THashMap<TNodeId, TVector<TNodeId>>& mod2Srcs);

    void RenderMsvsSolution(size_t version, const TStringBuf& name, const TStringBuf& dir);
    void RenderIDEProj(const TStringBuf& type, const TStringBuf& name, const TStringBuf& dir);

    void DumpMetaData();

    void DumpOwners();

    TNodeId GetUserTarget(const TStringBuf& target) const;

    bool LoadImpl(const TFsPath& file);
    bool Load(const TFsPath& file);
    bool LoadPatch();
    void LoadUids(TUidsCachable* uidsCachable);
    void Save(const TFsPath& file, bool delayed);
    void SaveUids(TUidsCachable* uidsCachable);
    void CommitCaches();

    TModuleResolveContext GetModuleResolveContext(const TModule& mod);
    TRestoreContext GetRestoreContext();
    TTraverseStartsContext GetTraverseStartsContext() const noexcept;
    TFileProcessContext GetFileProcessContext(TModule* module, TAddDepAdaptor& node);
};
