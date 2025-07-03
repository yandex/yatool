#pragma once

#include "add_iter.h"
#include "build_result.h"
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

#include <asio/awaitable.hpp>
#include <asio/thread_pool.hpp>
#include <asio/strand.hpp>

using TConfigurationExecutor = asio::strand<asio::any_io_executor>;

using TDependsToModulesClosure = THashMap<TString, TVector<TNodeId>>;

class TNodeEdgesComparator {
private:
    EMakeNodeType FromType;
    const TDepGraph& Graph;

public:
    TNodeEdgesComparator(TConstDepNodeRef node);

    bool operator() (const TDepGraph::TEdge& edge1, const TDepGraph::TEdge& edge2) const noexcept;
};

class ITargetConfigurator {
public:
    virtual asio::awaitable<void> AddStartTarget(TConfigurationExecutor exec, const TString& dir, const TString& tag = "", bool followRecurses = true) = 0;
    virtual asio::awaitable<void> AddTarget(TConfigurationExecutor exec, const TString& dir) = 0;
};

class TYMake final : public ITargetConfigurator {
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
    bool DependsToModulesClosureCollected{false};

    NYndex::TYndex Yndex;

    TModules Modules;
    TCommands Commands;

private:
    TFsPath DepCacheTempFile;      // Name of temporary file with delayed save data
    TFsPath DMCacheTempFile;      // Name of temporary file with delayed save data
    TFsPath UidsCacheTempFile;      // Name of temporary file with delayed save data
    TString PrevDepsFingerprint;
    TString CurrDepsFingerprint;
    bool FSCacheLoaded_{false};
    bool DepsCacheLoaded_{false};
    bool DMCacheLoaded_{false};
    bool JSONCacheLoaded_{false};
    bool UidsCacheLoaded_{false};
    bool DependsToModulesClosureLoaded_{false};

    TVector<ui32> PrevStartDirs_;
    TVector<ui32> CurStartDirs_;
    TVector<TTarget> PrevStartTargets_;
    bool HasGraphStructuralChanges_{false};
    TStringBuf ExportLang_{"?"};
    int FillModule2Nodes_{-1};
    int CheckKVP_{-1};

    TVector<ui32> PreserveStartTargets() const;
    void FixStartTargets(const TVector<ui32>& elemIds);
    bool TryLoadUids(TUidsCachable* uidsCachable);
    void TransferStartDirs();

    void AnalyzeGraphChanges(IChanges& changes);
    bool LoadDependsToModulesClosure(IInputStream* input);
    bool SaveDependsToModulesClosure(IOutputStream* output);
public:
    explicit TYMake(TBuildConfiguration& conf);
    void PostInit(); // Call this after Load: this may rely on loaded symbol table
    ~TYMake();

    // Returns true if directory loops found
    bool DumpLoops();
    asio::awaitable<void> BuildDepGraph(TConfigurationExecutor exec);
    void CreateRecurseGraph();
    bool InitTargets();
    void AddRecursesToStartTargets();
    void AddModulesToStartTargets();
    void ComputeDependsToModulesClosure();
    void GetDependsToModulesClosure();
    asio::awaitable<void> AddStartTarget(TConfigurationExecutor exec, const TString& dir, const TString& tag = "", bool followRecurses = true) override;
    asio::awaitable<void> AddTarget(TConfigurationExecutor exec, const TString& dir) override;
    void SortAllEdges();
    void CheckBlacklist();
    void CheckIsolatedProjects();
    void CheckStartDirsChanges();
    void DumpDependentDirs(IOutputStream& cmsg, bool skipDepends = false) const;
    void DumpSrcDeps(IOutputStream& cmsg);
    void PrintTargetDeps(IOutputStream& cmsg) const;
    void DumpBuildTargets(IOutputStream& cmsg) const;
    void DumpTestDart(IOutputStream& cmsg);
    void DumpJavaDart(IOutputStream& cmsg);
    void DumpMakeFilesDart(IOutputStream& cmsg);
    void ReportConfigureEvents();
    void ReportForeignPlatformEvents();
    void ReportGraphBuildStats();
    void ReportModulesStats();
    void ReportMakeCommandStats();
    void FindLostIncludes();
    TMaybe<EBuildResult> ApplyDependencyManagement();

    void ListTargetResults(const TTarget& startTarget, TVector<TNodeId>& dirMods, TVector<TNodeId>& globSrcs) const;
    bool ResolveRelationTargets(const TVector<TString>& targets, THashSet<TNodeId>& result);

    // Debug functions
    void DumpGraph();
    void FindPathBetween(const TVector<TString>& from,
                         const TVector<TString>& to);
    void FindMissingPeerdirs();
    void AssignSrcsToModules(THashMap<TNodeId, TVector<TNodeId>>& mod2Srcs);

    void DumpMetaData();

    void DumpOwners();

    TNodeId GetUserTarget(const TStringBuf& target) const;

    bool LoadImpl(const TFsPath& file);
    bool Load(const TFsPath& file);
    bool LoadPatch();
    bool LoadUids(TUidsCachable* uidsCachable);
    void Save(const TFsPath& file, bool delayed);
    void Compact();
    void SaveStartDirs(TCacheFileWriter& writer);
    void SaveStartTargets(TCacheFileWriter& writer);
    void SetStartTargetsFromCache() {
        StartTargets = std::move(PrevStartTargets_);
        for (const auto& target: StartTargets) {
            if (IsModuleType(Graph.Get(target.Id)->NodeType)) {
                ModuleStartTargets.insert(target);
            }
        }
    }
    void SaveUids(TUidsCachable* uidsCachable);
    TCacheFileReader::EReadResult LoadDependencyManagementCache(const TFsPath& cacheFile);
    bool SaveDependencyManagementCache(const TFsPath& cacheFile, TFsPath* tempCacheFile = nullptr);
    void CommitCaches();
    void JSONCacheLoaded(bool jsonCacheLoaded);
    void FSCacheMonEvent() const;
    void DepsCacheMonEvent() const;
    void JSONCacheMonEvent() const;
    void UidsCacheMonEvent() const;
    void GraphChangesPredictionEvent() const;
    void ComputeReachableNodes();
    bool CanBypassConfigure() const {
        // --xcompletely-trust-fs-cache can't be passed without --patch-path
        return Conf.ShouldUseGrandBypass() && Conf.CompletelyTrustFSCache && !HasGraphStructuralChanges_;
    }
    void UpdateExternalFilesChanges();
    void UpdateUnreachableExternalFileChanges();

    TModuleResolveContext GetModuleResolveContext(const TModule& mod);
    TRestoreContext GetRestoreContext();
    TTraverseStartsContext GetTraverseStartsContext() const noexcept;
    TFileProcessContext GetFileProcessContext(TModule* module, TAddDepAdaptor& node);

    const TStringBuf& GetExportLang() {
        if (ExportLang_ == "?") {
            ExportLang_ = Conf.CommandConf.EvalValue("EXPORT_LANG");
        }
        return ExportLang_;
    }

    bool GetFillModule2Nodes() {
        if (FillModule2Nodes_ < 0) {
            FillModule2Nodes_ = NYMake::IsTrue(Conf.CommandConf.EvalValue("FILL_MODULE2NODES")) ? 1 : 0;
        }
        return FillModule2Nodes_ > 0;
    }

    bool GetCheckKVP() {
        if (CheckKVP_ < 0) {
            CheckKVP_ = NYMake::IsTrue(Conf.CommandConf.EvalValue("CHECK_KVP")) ? 1 : 0;
        }
        return CheckKVP_ > 0;
    }
};
