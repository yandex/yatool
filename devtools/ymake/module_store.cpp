#include "module_store.h"
#include "module_state.h"
#include "dependency_management.h"
#include "json_saveload.h"

#include <devtools/ymake/diag/progress_manager.h>

#include <util/stream/format.h>

namespace {
    const TStringBuf DM_VAR_NAMES[] = {
        NVariableDefs::VAR_NON_NAMAGEABLE_PEERS,
        NVariableDefs::VAR_DART_CLASSPATH_DEPS,
        NVariableDefs::VAR_MANAGED_PEERS,
        NVariableDefs::VAR_MANAGED_PEERS_CLOSURE,
        NVariableDefs::VAR_DART_CLASSPATH,
        NVariableDefs::VAR_UNITTEST_MOD,
    };
}

TModule& TModules::Create(const TStringBuf& dir, const TStringBuf& makefile, const TStringBuf& tag) {
    TModule* module = new TModule(Symbols.FileConf.GetStoredName(dir), makefile, tag, CreationContext);
    YDIAG(V) << "Created module: " << makefile << ":" << tag << Endl;
    ModulesStore.emplace(module);
    return *module;
}

TModule* TModules::Get(ui32 id) {
    const auto iter = ModulesById.find(id);
    return iter != ModulesById.end() ? iter->second : nullptr;
}

TStringBuf TModules::ResultKey(const TModule& module) const {
    TFileView dir = module.GetDir();
    if (dir.InSrcDir()) {
        return dir.CutType();
    }
    return TStringBuf();
}

void TModules::NotifyMakefileReparsed(ui32 makefileId) {
    if (Loaded) {
        ReparsedMakefiles.insert(makefileId);
    }
}

TModules::TModules(TSymbols& symbols, const TPeersRules& rules, TBuildConfiguration& conf)
    : Symbols(symbols)
    , PeersRules(rules)
    , CreationContext({SharedEntriesByMakefileId, Symbols, conf.CommandConf, PeersRules})
    , RootModule(Create("$B", TStringBuf("$U/root"), {}))
{
    // Bypass validity checks
    RootModule.Id = 0;
}

void TModules::Commit(TModule& module) {
    AssertEx(module.HasId(), "Attempt to commit module without Id");

    auto id = module.GetId();
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
    ModulesById[id] = &module;
    module.Committed = true;
    YDIAG(V) << "Committed module: " << module.GetMakefile() << " as " << module.GetFileName() << " (" << id << ")" << Endl;

    TProgressManager::Instance()->UpdateConfModulesTotal(ModulesById.size());
}

void TModules::Destroy(TModule& module) {
    if (module.HasId() && module.Committed) {
        ModulesById.erase(module.GetId());
        ModuleIncludesById.erase(module.GetId());
    }
    ModulesStore.erase(&module);
    delete &module;
}

void TModules::Load(IInputStream* input) {
    TModulesSaver saver;
    saver.Load(input);
    for (auto&& item : saver.Data) {
        auto module = new TModule(std::move(item), CreationContext);
        YDIAG(V) << "Loaded module: " << module->GetName() << Endl;
        ModulesStore.emplace(module);
        Commit(*module);
    }
    Loaded = true;
}

void TModules::Load(const TBlob& blob) {
    TMemoryInput input(blob.Data(), blob.Length());
    Load(&input);
}

void TModules::LoadDMCache(IInputStream* input, const TDepGraph& graph) {
    ResetTransitiveInfo();

    ui32 modulesCount = LoadFromStream<ui32>(input);
    for (ui32 i = 0; i < modulesCount; ++i) {
        ui32 modId = LoadFromStream<ui32>(input);
        auto module = Get(modId);
        Y_ASSERT(module);

        TVector<ui32> uniqPeersIds, directPeersIds;
        THashMap<TString, TString> dmVars;
        bool isPeersComplete;
        ::Load(input, uniqPeersIds);
        ::Load(input, directPeersIds);
        ::Load(input, dmVars);
        ::Load(input, isPeersComplete);
        auto& moduleLists = GetModuleNodeIds(modId);
        for (auto peer : uniqPeersIds) {
            TFileView peerFileView = Symbols.FileConf.GetName(peer);
            TNodeId nodeId = graph.GetFileNode(peerFileView).Id();
            GetNodeListStore().AddToList(moduleLists.UniqPeers, nodeId);
        }
        for (auto peer : directPeersIds) {
            TFileView peerFileView = Symbols.FileConf.GetName(peer);
            TNodeId nodeId = graph.GetFileNode(peerFileView).Id();
            GetNodeListStore().AddToList(moduleLists.ManagedDirectPeers, nodeId);
        }
        for (const auto& varName : DM_VAR_NAMES) {
            if (dmVars.contains(varName)) {
                module->Set(varName, dmVars[varName]);
            } else {
                module->Set(varName, TString());
            }
        }
        if (isPeersComplete) {
            module->SetPeersComplete();
        }
    }
}

void TModules::Save(IOutputStream* output) {
    TModulesSaver saver;
    saver.Data.reserve(ModulesById.size());
    SaveFilteredModules(saver);
    saver.Save(output);
    ClearRawIncludes();
}

void TModules::SaveDMCache(IOutputStream* output, const TDepGraph& graph) {
    ui32 modCount = ModulesById.size();
    ::Save(output, modCount);

    for (const auto& [modId, module] : ModulesById) {
        ::Save(output, modId);

        const auto moduleLists = GetModuleNodeLists(modId);
        TVector<ui32> uniqPeersIds, managedDirectPeersIds;
        for (auto peer : moduleLists.UniqPeers()) {
            uniqPeersIds.push_back(graph.Get(peer)->ElemId);
        }
        for (auto peer : moduleLists.ManagedDirectPeers()) {
            managedDirectPeersIds.push_back(graph.Get(peer)->ElemId);
        }
        THashMap<TString, TString> dmVars;
        for (auto varName : DM_VAR_NAMES) {
            TStringBuf value = module->Get(varName);
            if (!value.empty()) {
                dmVars.emplace(varName, value);
            }
        }
        ::Save(output, uniqPeersIds);
        ::Save(output, managedDirectPeersIds);
        ::Save(output, dmVars);
        ::Save(output, bool(module->IsPeersComplete()));
    }
}

void TModules::SaveFilteredModules(TModulesSaver& saver) {
    for (const auto [id, module]: ModulesById) {
        if (Loaded && module->IsLoaded() && ReparsedMakefiles.contains(module->GetMakefileId())) {
            YDIAG(VV) << "Ignore outdated module: " << module->GetName() << Endl;
            continue;
        }
        saver.Data.emplace_back(*module);
    }
}

void TModules::ClearRawIncludes() {
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
    ui64 countTotal = 0;
    ui64 countLoaded = 0;
    ui64 countParsed = 0;
    ui64 countOutdated = 0;
    ui64 countAccessed = 0;
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
    ModulesById.clear();
    for (auto modPtr: ModulesStore) {
         delete modPtr;
    }
    ModulesStore.clear();
}

TModules::~TModules() {
    Clear();
}

TDependencyManagementModuleInfo& TModules::GetExtraDependencyManagementInfo(ui32 modId) {
    return ModuleDMInfoById[modId];
}

const TDependencyManagementModuleInfo* TModules::FindExtraDependencyManagementInfo(ui32 modId) {
    return ModuleDMInfoById.FindPtr(modId);
}

THolder<TOwnEntries> TModules::ExtractSharedEntries(ui32 makefileId) {
    auto it = SharedEntriesByMakefileId.find(makefileId);
    if (it == SharedEntriesByMakefileId.end()) {
        return {};
    }
    return std::move(it->second);
}

TModuleNodeLists TModules::GetModuleNodeLists(ui32 moduleId) {
    return {NodeListStore, ModuleIncludesById[moduleId].NodeIds};
}

TModuleNodeLists TModules::GetModuleNodeLists(ui32 moduleId) const {
    return {NodeListStore, ModuleIncludesById.at(moduleId).NodeIds};
}

TModuleNodeIds& TModules::GetModuleNodeIds(ui32 moduleId) {
    return ModuleIncludesById[moduleId].NodeIds;
}

const TModuleNodeIds& TModules::GetModuleNodeIds(ui32 moduleId) const {
    return ModuleIncludesById.at(moduleId).NodeIds;
}

TGlobalVars& TModules::GetGlobalVars(ui32 moduleId) {
    return ModuleIncludesById[moduleId].GlobalVars;
}

const TGlobalVars& TModules::GetGlobalVars(ui32 moduleId) const {
    return ModuleIncludesById.at(moduleId).GlobalVars;
}

void TModules::ClearModuleLateOuts(ui32 moduleId) {
    ModuleLateOutsById.Insert(moduleId, TVector<TString>());
}

TVector<TString>& TModules::GetModuleLateOuts(ui32 moduleId) {
    return ModuleLateOutsById.InsertIfAbsent(moduleId, TVector<TString>());
}

const TVector<TString>& TModules::GetModuleLateOuts(ui32 moduleId) const {
    return ModuleLateOutsById.GetBucketForKey(moduleId).GetUnsafe(moduleId);
}

void TModules::ResetTransitiveInfo() {
    ModuleIncludesById.clear();
}
