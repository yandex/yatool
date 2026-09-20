#include "module_store.h"
#include "module_state.h"
#include "dependency_management.h"
#include "frozen_module_store.h"

#include <devtools/ymake/diag/progress_manager.h>

#include <util/generic/yexception.h>
#include <util/stream/format.h>

TModule& TModules::Create(const TStringBuf& dir, const TStringBuf& makefile, const TStringBuf& tag) {
    TModule* module = new TModule(Symbols.FileConf.GetStoredName(dir), makefile, tag, CreationContext);
    YDIAG(V) << "Created module: " << makefile << ":" << tag << Endl;
    ModulesStore.emplace(module);
    return *module;
}

TModule* TModules::Get(TFileElemId id) {
    {
        TLightReadGuard guard(MaterializationLock);
        const auto& modules = ModulesById;
        if (const auto iter = modules.find(id); iter != modules.end()) {
            return iter->second;
        }
    }

    // Another thread may have materialized the group after our read miss.
    TLightWriteGuard guard(MaterializationLock);
    if (const auto iter = ModulesById.find(id); iter != ModulesById.end()) {
        return iter->second;
    }

    FrozenModules->MaterializeModule(id);
    const auto materialized = ModulesById.find(id);
    return materialized != ModulesById.end() ? materialized->second : nullptr;
}

bool TModules::Contains(TFileElemId id) const {
    TLightReadGuard guard(MaterializationLock);
    return ContainsUnlocked(id);
}

bool TModules::ContainsUnlocked(TFileElemId id) const {
    return ModulesById.contains(id) || FrozenModules->Contains(id);
}

TStringBuf TModules::ResultKey(const TModule& module) const {
    TFileView dir = module.GetDir();
    if (dir.InSrcDir()) {
        return dir.CutType();
    }
    return TStringBuf();
}

void TModules::NotifyMakefileReparsed(TFileElemId makefileId) {
    if (Loaded) {
        ReparsedMakefiles.insert(makefileId);
    }
}

TModules::TModules(TSymbols& symbols, const TPeersRules& rules, TBuildConfiguration& conf)
    : Symbols(symbols)
    , PeersRules(rules)
    , CreationContext({SharedEntriesByMakefileId, Symbols, conf.CommandConf, PeersRules})
    , RootModule(Create("$B", TStringBuf("$U/root"), {}))
    , FrozenModules(MakeHolder<TFrozenModuleStore>(*this))
{
    // Bypass validity checks
    RootModule.Id = TFileElemId();
}

void TModules::Commit(TModule& module) {
    TLightWriteGuard guard(MaterializationLock);
    CommitUnlocked(module);
}

void TModules::CommitUnlocked(TModule& module) {
    AssertEx(module.HasId(), "Attempt to commit module without Id");

    auto id = module.GetId();
    const bool wasLogical = ContainsUnlocked(id);
    if (ModulesById.contains(id)) {
        TModule* oldMod = ModulesById[id];
        if (oldMod == &module) {
            ModuleIncludesById.erase(id);
            oldMod->PeersComplete = false;
            YDIAG(V) << "Re-Committed module: " << module.GetMakefile() << " as " << module.GetFileName() << " (" << id << ")" << Endl;
            return;
        } else if (ModulesById[id]->IsLoaded()) {
            // We let override cached module by parsed one
            AssertEx(!module.IsLoaded(), "Attempt to commit new cached module " + ToString(id));
            ModulesStore.erase(oldMod);
            ModuleIncludesById.erase(id);
            delete oldMod;
        } else {
            AssertEx(false, "Attempt to commit module with duplicate id " + ToString(id));
        }
    }
    if (!module.IsLoaded() && FrozenModules->HasDescriptor(id)) {
        FrozenModules->Suppress(id);
    }
    ModulesById[id] = &module;
    module.ComputeConfigVars();
    module.Committed = true;
    if (!wasLogical) {
        ++LogicalModulesCount;
    }
    YDIAG(V) << "Committed module: " << module.GetMakefile() << " as " << module.GetFileName() << " (" << id << ")" << Endl;

    TProgressManager::Instance()->UpdateConfModulesTotal(LogicalModulesCount);
}

void TModules::Destroy(TModule& module) {
    TLightWriteGuard guard(MaterializationLock);
    if (module.HasId() && module.Committed) {
        if (module.IsLoaded() && FrozenModules->HasDescriptor(module.GetId())) {
            FrozenModules->Suppress(module.GetId());
        }
        ModulesById.erase(module.GetId());
        ModuleIncludesById.erase(module.GetId());
        Y_ASSERT(LogicalModulesCount != 0);
        --LogicalModulesCount;
    }
    ModulesStore.erase(&module);
    delete &module;
}

void TModules::Load(IInputStream* input) {
    Load(TBlob::FromStringSingleThreaded(input->ReadAll()));
}

void TModules::Load(const TBlob& blob) {
    TLightWriteGuard guard(MaterializationLock);
    Y_ENSURE(!Loaded && ModulesById.empty(), "Modules cache is already loaded");
    FrozenModules->Load(blob);
    LogicalModulesCount = FrozenModules->ModuleCount();
    Loaded = true;
}

void TModules::LoadDMCache(IInputStream* input, const TDepGraph& graph) {
    TLightWriteGuard guard(MaterializationLock);
    ResetTransitiveInfo();
    FrozenModules->LoadDMCache(input, graph);
}

void TModules::Save(IOutputStream* output) {
    TLightWriteGuard guard(MaterializationLock);
    FrozenModules->Save(output);
    ClearRawIncludesUnlocked();
}

void TModules::SaveDMCache(IOutputStream* output, const TDepGraph& graph) {
    TLightWriteGuard guard(MaterializationLock);
    FrozenModules->SaveDMCache(output, graph);
}

void TModules::ClearRawIncludes() {
    TLightWriteGuard guard(MaterializationLock);
    ClearRawIncludesUnlocked();
}

void TModules::ClearRawIncludesUnlocked() {
    FrozenModules->ClearRawIncludes();
    for (auto [id, module] : ModulesById) {
        module->RawIncludes.clear();
    }
}

void TModules::Compact() {
    TVector<TModule*> outdated;
    for (const auto [id, module]: ModulesById) {
        if (Loaded && module->IsLoaded() && ReparsedMakefiles.contains(module->GetMakefileId())) {
             outdated.push_back(module);
        }
        if (!module->IsLoaded()) {
             module->TrimVars();
        }
    }
    for (auto module: outdated) {
         Destroy(*module);
    }
}

void TModules::ReportStats() const {
    TLightWriteGuard guard(MaterializationLock);
    ui64 countTotal = 0;
    ui64 countLoaded = 0;
    ui64 countParsed = 0;
    ui64 countOutdated = 0;
    ui64 countAccessed = 0;
    FrozenModules->AccumulateStats(countTotal, countLoaded, countOutdated);
    for (const auto [id, module]: ModulesById) {
        if (Loaded && module->IsLoaded()) {
            ++countLoaded;
            if (ReparsedMakefiles.contains(module->GetMakefileId())) {
                ++countOutdated;
                continue;
            }
        } else {
            ++countParsed;
        }
        if (module->IsAccessed()) {
            ++countAccessed;
        }
        ++countTotal;
    }

    Stats.Set(NStats::EModulesStats::Total, countTotal);
    Stats.Set(NStats::EModulesStats::Loaded, countLoaded);
    Stats.Set(NStats::EModulesStats::Parsed, countParsed);
    Stats.Set(NStats::EModulesStats::Outdated, countOutdated);
    Stats.Set(NStats::EModulesStats::Accessed, countAccessed);

    Stats.Report();
    TProgressManager::Instance()->ForceUpdateConfModulesDoneTotal(countLoaded + countParsed, countTotal);
}

void TModules::Clear() {
    TLightWriteGuard guard(MaterializationLock);
    ModulesById.clear();
    for (auto modPtr: ModulesStore) {
         delete modPtr;
    }
    ModulesStore.clear();
    FrozenModules->Clear();
    LogicalModulesCount = 0;
}

TModules::~TModules() {
    Clear();
}

TDependencyManagementModuleInfo& TModules::GetExtraDependencyManagementInfo(TFileElemId modId) {
    return ModuleDMInfoById[modId];
}

const TDependencyManagementModuleInfo* TModules::FindExtraDependencyManagementInfo(TFileElemId modId) {
    return ModuleDMInfoById.FindPtr(modId);
}

THolder<TOwnEntries> TModules::ExtractSharedEntries(TFileElemId makefileId) {
    TLightWriteGuard guard(MaterializationLock);
    FrozenModules->MaterializeMakefile(makefileId);
    auto it = SharedEntriesByMakefileId.find(makefileId);
    if (it == SharedEntriesByMakefileId.end()) {
        return {};
    }
    return std::move(it->second);
}

TModuleNodeLists TModules::GetModuleNodeLists(TFileElemId moduleId) {
    return {NodeListStore, ModuleIncludesById[moduleId].NodeIds};
}

TModuleNodeLists TModules::GetModuleNodeLists(TFileElemId moduleId) const {
    return {NodeListStore, ModuleIncludesById.at(moduleId).NodeIds};
}

TModuleNodeIds& TModules::GetModuleNodeIds(TFileElemId moduleId) {
    return ModuleIncludesById[moduleId].NodeIds;
}

const TModuleNodeIds& TModules::GetModuleNodeIds(TFileElemId moduleId) const {
    return ModuleIncludesById.at(moduleId).NodeIds;
}

TGlobalVars& TModules::GetGlobalVars(TFileElemId moduleId) {
    return ModuleIncludesById[moduleId].GlobalVars;
}

const TGlobalVars& TModules::GetGlobalVars(TFileElemId moduleId) const {
    return ModuleIncludesById.at(moduleId).GlobalVars;
}

void TModules::ClearModuleLateOuts(TFileElemId moduleId) {
    ModuleLateOutsById.Insert(moduleId, TVector<TString>());
}

TVector<TString>& TModules::GetModuleLateOuts(TFileElemId moduleId) {
    return ModuleLateOutsById.InsertIfAbsent(moduleId, TVector<TString>());
}

const TVector<TString>& TModules::GetModuleLateOuts(TFileElemId moduleId) const {
    return ModuleLateOutsById.GetBucketForKey(moduleId).GetUnsafe(moduleId);
}

void TModules::ResetTransitiveInfo() {
    ModuleIncludesById.clear();
}
