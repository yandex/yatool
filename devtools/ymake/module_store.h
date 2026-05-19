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
    THashMap<TElemId_Underlying, TModule*, TIdentity> ModulesById;
    TModule& RootModule;

    TStringBuf ResultKey(const TModule& module) const;

    TNodeListStore NodeListStore;
    THashMap<TElemId_Underlying, TTransitiveModuleInfo, TIdentity> ModuleIncludesById;
    THashMap<TFileElemId, TDependencyManagementModuleInfo> ModuleDMInfoById;

    TConcurrentHashMap<TFileElemId, TVector<TString>> ModuleLateOutsById;

    mutable NStats::TModulesStats Stats = NStats::TModulesStats("TModules stats");

    bool Loaded = false;
    THashSet<TFileElemId> ReparsedMakefiles;

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
    TModule* Get(TFileElemId id);

    /// Try to locate module by ElemId
    const TModule* Get(TFileElemId id) const {
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

    void NotifyMakefileReparsed(TFileElemId makefileId);

    TNodeListStore& GetNodeListStore() noexcept {
        return NodeListStore;
    }

    const TNodeListStore& GetNodeListStore() const noexcept {
        return NodeListStore;
    }

    TDependencyManagementModuleInfo& GetExtraDependencyManagementInfo(TFileElemId modId);
    const TDependencyManagementModuleInfo* FindExtraDependencyManagementInfo(TFileElemId modId);

    THolder<TOwnEntries> ExtractSharedEntries(TFileElemId makefileId);;

    TModuleNodeLists GetModuleNodeLists(TFileElemId moduleId);
    TModuleNodeLists GetModuleNodeLists(TFileElemId moduleId) const;

    TModuleNodeIds& GetModuleNodeIds(TFileElemId moduleId);
    const TModuleNodeIds& GetModuleNodeIds(TFileElemId moduleId) const;

    TGlobalVars& GetGlobalVars(TFileElemId moduleId);

    const TGlobalVars& GetGlobalVars(TFileElemId moduleId) const;

    void ClearModuleLateOuts(TFileElemId moduleId);
    TVector<TString>& GetModuleLateOuts(TFileElemId moduleId);
    const TVector<TString>& GetModuleLateOuts(TFileElemId moduleId) const;

    TConcurrentHashMap<TFileElemId, TVector<TString>> TakeModulesLateOuts() {
        return std::move(ModuleLateOutsById);
    }

    void SetModulesLateOuts(TConcurrentHashMap<TFileElemId, TVector<TString>>&& lateOutsMap) {
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
