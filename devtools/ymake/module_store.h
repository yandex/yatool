#pragma once

#include "module_state.h"
#include "transitive_state.h"

#include <devtools/ymake/diag/stats.h>

#include <library/cpp/containers/concurrent_hash/concurrent_hash.h>
#include <util/generic/hash.h>

class TFileConf;
struct TDependencyManagementModuleInfo;

/// @brief Collection that owns all TModules and provides navigation among them.
/// Modules are created using 2-stage initialization:
/// - Create() methods allocate almost uninitialized modules
/// - Commit() method adds module to ElemId-based index
/// - Destroy() deletes module (both committed and non-committed)
class TModules {
private:
    using TResultOutputsMap = THashMap<TStringBuf, TString>;

    TSymbols& Symbols;
    const TPeersRules& PeersRules;
    TSharedEntriesMap SharedEntriesByMakefileId;
    TModulesSharedContext CreationContext;
    THashSet<TModule*> ModulesStore;
    THashMap<ui32, TModule*, TIdentity> ModulesById;
    TModule& RootModule;

    TStringBuf ResultKey(const TModule& module) const;

    TNodeListStore NodeListStore;
    THashMap<ui32, TTransitiveModuleInfo, TIdentity> ModuleIncludesById;
    THashMap<ui32, TDependencyManagementModuleInfo> ModuleDMInfoById;

    TConcurrentHashMap<ui32, TVector<TString>> ModuleLateOutsById;

    mutable NStats::TModulesStats Stats = NStats::TModulesStats("TModules stats");

    bool Loaded = false;
    THashSet<ui32> ReparsedMakefiles;

    class TModulesSaver {
    public:
        TVector<TModuleSavedState> Data;
        Y_SAVELOAD_DEFINE(Data);
    };

public:
    /// Contructs the collection with just RootModule in it
    explicit TModules(TSymbols& symbols, const TPeersRules& rules, TBuildConfiguration& conf);

    /// Create module in a directory with unset makefile and default tag
    TModule& Create(const TStringBuf& dir) {
        return Create(dir, TStringBuf("$U/unset"));
    }

    /// Create module in a directory with set makefile and default tag
    TModule& Create(const TStringBuf& dir, const TStringBuf& makefile) {
        return Create(dir, makefile, {});
    }

    /// Create module in a directory with set makefile and tag
    TModule& Create(const TStringBuf& dir, const TStringBuf& makefile, const TStringBuf& tag);

    /// Get reference to root module
    TModule& GetRootModule() {
        return RootModule;
    }

    /// Add module to cross-reference collection
    /// Major module properties (FileName, Id, NodeType) shouldn't change after this call
    void Commit(TModule& module);

    /// Completely delete the module
    /// One shouldn't use module after this call: the reference becomes invalid
    void Destroy(TModule& module);

    /// Try to locate module by ElemId
    TModule* Get(ui32 id);

    /// Try to locate module by ElemId
    const TModule* Get(ui32 id) const {
        return const_cast<TModules*>(this)->Get(id);
    }

    /// Load cached modules from stream
    /// This requires Symbols to be already loaded
    void Load(IInputStream* input);

    /// Load cached modules from blob
    /// This requires Symbols to be already loaded
    void Load(const TBlob& blob);

    /// Load dependency management cache
    /// This requires Symbols and Graph to be already loaded
    void LoadDMCache(IInputStream* input, const TDepGraph& graph);

    /// Save modules cache
    /// This shall be done before Symbols are saved since it may write to symTab
    void Save(IOutputStream* output);

    /// Save dependency management cache
    /// This shall be done after Modules table are saved and dep management algorithm is done
    void SaveDMCache(IOutputStream* output, const TDepGraph& graph);

    void NotifyMakefileReparsed(ui32 makefileId);

    TNodeListStore& GetNodeListStore() noexcept {
        return NodeListStore;
    }

    const TNodeListStore& GetNodeListStore() const noexcept {
        return NodeListStore;
    }

    TDependencyManagementModuleInfo& GetExtraDependencyManagementInfo(ui32 modId);
    const TDependencyManagementModuleInfo* FindExtraDependencyManagementInfo(ui32 modId);

    THolder<TOwnEntries> ExtractSharedEntries(ui32 makefileId);;

    TModuleNodeLists GetModuleNodeLists(ui32 moduleId);
    TModuleNodeLists GetModuleNodeLists(ui32 moduleId) const;

    TModuleNodeIds& GetModuleNodeIds(ui32 moduleId);
    const TModuleNodeIds& GetModuleNodeIds(ui32 moduleId) const;

    TGlobalVars& GetGlobalVars(ui32 moduleId);

    const TGlobalVars& GetGlobalVars(ui32 moduleId) const;

    void ClearModuleLateOuts(ui32 moduleId);
    TVector<TString>& GetModuleLateOuts(ui32 moduleId);
    const TVector<TString>& GetModuleLateOuts(ui32 moduleId) const;

    TConcurrentHashMap<ui32, TVector<TString>> TakeModulesLateOuts() {
        return std::move(ModuleLateOutsById);
    }

    void SetModulesLateOuts(TConcurrentHashMap<ui32, TVector<TString>>&& lateOutsMap) {
        ModuleLateOutsById = std::move(lateOutsMap);
    }

    void ResetTransitiveInfo();

    decltype(ModulesById)::const_iterator begin() const {
        return ModulesById.begin();
    }

    decltype(ModulesById)::const_iterator end() const {
        return ModulesById.end();
    }

    void ReportStats() const;

    void Clear();

    void ClearRawIncludes();

    void Compact();

    ~TModules();

private:
    void SaveFilteredModules(TModulesSaver& saver);
};
