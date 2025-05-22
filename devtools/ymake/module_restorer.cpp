#include "module_restorer.h"
#include "ymake.h"
#include "mine_variables.h"
#include "macro_processor.h"
#include "prop_names.h"
#include "dependency_management.h"
#include "global_vars_collector.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/compact_graph/peer_collector.h>

#include <library/cpp/iterator/mapped.h>

#include <util/generic/string.h>
#include <util/generic/strbuf.h>

namespace {
    template<typename TCollector>
    class TDirectReachablePeerCollectingVisitor: public TPeerCollectingVisitor<TCollector> {
    public:
        using TBase = TPeerCollectingVisitor<TCollector>;
        using TState = typename TBase::TState;

        TDirectReachablePeerCollectingVisitor(TCollector& collector): TBase{collector} {}

        bool AcceptDep(TState& state) {
            return TBase::AcceptDep(state) && IsDirectPeerdirDep(state.Top().CurDep());
        }
    };

    class TTransitivePeersCollector {
    private:
        TRestoreContext& RestoreContext;

    public:
        using TStateItem = TGraphIteratorStateItemBase<true>;

        TTransitivePeersCollector(TRestoreContext& restoreContext)
            : RestoreContext{restoreContext} {
        }

        bool Start(TStateItem& parentItem) {
            TModule& parent = InitModule(RestoreContext.Modules, RestoreContext.Conf.CommandConf, parentItem.Node());
            return !parent.IsPeersComplete();
        }

        void Finish(TStateItem& parentItem, void*) {
            TModule* parent = RestoreContext.Modules.Get(parentItem.Node()->ElemId);
            Y_ASSERT(parent);

            auto& moduleNodeIds = RestoreContext.Modules.GetModuleNodeIds(parent->GetId());
            for (const auto& dep : parentItem.Node().Edges()) {
                if (!IsGlobalSrcDep(dep)) {
                    continue;
                }

                RestoreContext.Modules.GetNodeListStore().AddToList(moduleNodeIds.GlobalSrcsIds, dep.To().Id());
                if (AnyOf(dep.To().Edges().begin(), dep.To().Edges().end(), [](auto glSrcDep) { return *glSrcDep == EDT_BuildCommand; })) {
                    moduleNodeIds.DepCommandIds.insert(dep.To().Id());
                }
            }

            parent->SetPeersComplete();
        }

        void Collect(TStateItem& parentItem, TConstDepNodeRef peerNode) {
            TModule* parent = RestoreContext.Modules.Get(parentItem.Node()->ElemId);
            Y_ASSERT(parent);

            const TModule* peer = RestoreContext.Modules.Get(peerNode->ElemId);
            Y_ASSERT(peer);

            if (parent->GhostPeers.contains(peer->GetDirId())) {
                return;
            }

            const TNodeId peerNodeId = RestoreContext.Graph.GetNodeById(peer->GetNodeType(), peer->GetId()).Id();

            auto& parentNodeIds = RestoreContext.Modules.GetModuleNodeIds(parent->GetId());
            if (peer->PassPeers()) {
                auto& peerIds = RestoreContext.Modules.GetModuleNodeIds(peer->GetId());
                parentNodeIds.DepCommandIds.insert(peerIds.DepCommandIds.begin(), peerIds.DepCommandIds.end());
                RestoreContext.Modules.GetNodeListStore().MergeLists(parentNodeIds.GlobalSrcsIds, peerIds.GlobalSrcsIds);
                RestoreContext.Modules.GetNodeListStore().MergeLists(parentNodeIds.UniqPeers, peerIds.UniqPeers);
            }

            parentNodeIds.LocalPeers.insert(peerNodeId);
            RestoreContext.Modules.GetNodeListStore().AddToList(parentNodeIds.UniqPeers, peerNodeId);
        }
    };

    class TMineFilesGroupVarsVisitor: public TNoReentryStatsConstVisitor<> {
    public:
        using TBase = TNoReentryStatsConstVisitor<>;
        using TState = TBase::TState;
        using TStateItem = TBase::TStateItem;

        bool AcceptDep(TState& state) {
            const auto& dep = state.NextDep();
            return TBase::AcceptDep(state)
                && dep.To()->NodeType != EMNT_Directory
                && !IsModuleType(dep.To()->NodeType)
                && !IsLateGlobPropDep(dep);
        }

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }

            if (IsModule(state.Top())) {
                ModuleElemId = state.TopNode()->ElemId;
                return true;
            }

            if (!state.HasIncomingDep()) {
                return true;
            }

            if (IsIndirectSrcDep(state.IncomingDep())
                || IsDartPropDep(state.IncomingDep())
                || IsAllSrcsPropDep(state.IncomingDep())
            ) {
                if (IsCurrentModuleFilesGroupVar(state.Top())) {
                    GetOrMineVarValue(state.TopNode());
                }
                return false;
            }

            return true;
        }

        const THashMap<TStringBuf, TUniqVector<TNodeId>>& GetMinedData() const noexcept {
            return MinedData;
        }

    private:
        bool IsCurrentModuleFilesGroupVar(const TStateItem& nodeState) {
            return ModuleElemId && GetId(nodeState.GetCmdName().GetStr()) == ModuleElemId;
        }

        enum class EStatementType {
            GLOB,
            GROUP_VAR,
        };

        EStatementType GetStatementType(const TDepGraph::TConstNodeRef& node) {
            auto varName = GetVarName(node);
            if (EqualToOneOf(varName, NProps::GLOB, NProps::LATE_GLOB)) {
                return EStatementType::GLOB;
            }
            return EStatementType::GROUP_VAR;
        }

        const TUniqVector<TNodeId>& GetOrMineVarValue(const TConstDepNodeRef& node) {
            switch (GetStatementType(node)) {
                case EStatementType::GLOB: {
                    if (!GlobFiles.contains(node.Id())) {
                        MineGlobData(node);
                    }
                    Y_ASSERT(GlobFiles.contains(node.Id()));
                    return GlobFiles[node.Id()];
                }
                case EStatementType::GROUP_VAR: {
                    auto varName = GetVarName(node);
                    if (!MinedData.contains(varName)) {
                        MineGroupVarData(node);
                    }
                    Y_ASSERT(MinedData.contains(varName));
                    return MinedData[varName];
                }
            }
            Y_UNREACHABLE();
        }

        void MineGroupVarData(const TConstDepNodeRef& node) {
            auto varName = GetVarName(node);
            TUniqVector<TNodeId> files;

            for (const auto& dep : node.Edges()) {
                switch (dep.To()->NodeType) {
                    case EMNT_File: {
                        files.Push(dep.To().Id());
                        break;
                    }
                    case EMNT_BuildCommand: {
                        GetOrMineVarValue(dep.To()).AddTo(files);
                        break;
                    }
                    default: /*nop*/;
                }
            }

            MinedData[varName] = std::move(files);
        }

        void MineGlobData(const TConstDepNodeRef& node) {
            TUniqVector<TStringBuf> varNames;
            TUniqVector<TNodeId> files;

            for (const auto&  dep: node.Edges()) {
                if (*dep != EDT_Property) {
                    continue;
                }

                switch (dep.To()->NodeType) {
                    case EMNT_File: {
                        files.Push(dep.To().Id());
                        break;
                    }
                    case EMNT_Property: {
                        const TStringBuf prop = TDepGraph::GetCmdName(dep.To()).GetStr();
                        if (GetPropertyName(prop) == NProps::REFERENCED_BY) {
                            varNames.Push(GetPropertyValue(prop));
                        }
                        break;
                    }
                    default: /*nop*/;
                }
            }

            for (auto var: varNames) {
                files.AddTo(MinedData[var]);
            }

            GlobFiles[node.Id()] = std::move(files);
        }

        static TStringBuf GetVarName(const TConstDepNodeRef& node) {
            return GetCmdName(TDepGraph::GetCmdName(node).GetStr());
        }

    private:
        THashMap<TStringBuf, TUniqVector<TNodeId>> MinedData;
        THashMap<TNodeId, TUniqVector<TNodeId>> GlobFiles;
        ui32 ModuleElemId = 0;
    };

    class TMineToolsVisitor: public TDirectPeerdirsConstVisitor<> {
    private:
        TModules& Modules;

        TModule* Parent = nullptr;
        TModule* Peer = nullptr;

    public:
        using TBase = TDirectPeerdirsConstVisitor<>;

        TMineToolsVisitor(TModules& modules) noexcept
            : Modules{modules} {
        }

        bool Enter(TState& state) {
            const bool fresh = TBase::Enter(state);
            if (!IsModule(state.Top())) {
                return fresh;
            }

            Parent = std::exchange(Peer, Modules.Get(state.Top().Node()->ElemId));
            Y_ASSERT(Peer);

            return fresh;
        }

        bool AcceptDep(TState& state) {
            return TBase::AcceptDep(state) && !IsTooldirDep(state.Top().CurDep());
        }

        void Leave(TState& state) {
            if (IsModule(state.Top())) {
                Y_ASSERT(Peer);
                const auto peerIter = state.Stack().rbegin();

                if (!Peer->IsToolsComplete()) {
                    MineModule(*Peer, peerIter->Node());
                }

                if (Parent) {
                    if (IsDirectToolDep(state.IncomingDep())) {
                        MineTooldir(*Parent, *Peer, state.Top().Node());
                    } else if (IsDirectPeerdirDep(state.IncomingDep())) {
                        MinePeerdir(*Parent, *Peer, state.Top().Node());
                    }
                }

                const auto iter = FindIf(
                    state.Stack().rbegin(),
                    state.Stack().rend(),
                    [&](const auto& stackItem) {
                        return IsModule(stackItem) && stackItem.Node()->ElemId != Peer->GetId() && stackItem.Node()->ElemId != Parent->GetId();
                    });

                TModule* grandparent = iter != state.Stack().rend() ? Modules.Get(iter->Node()->ElemId) : nullptr;
                Peer = std::exchange(
                    Parent,
                    grandparent);
            }
            TBase::Leave(state);
        }

        void MineModule(TModule& module, TConstDepNodeRef) {
            module.SetToolsComplete();
        }

        void MineTooldir(TModule& parent, TModule& peer, TConstDepNodeRef node) {
            auto& parentNodes = Modules.GetModuleNodeIds(parent.GetId());
            auto& peerNodes = Modules.GetModuleNodeIds(peer.GetId());
            Modules.GetNodeListStore().MergeLists(parentNodes.Tools, peerNodes.Tools);
            Modules.GetNodeListStore().AddToList(parentNodes.Tools, node.Id());
        }

        void MinePeerdir(TModule& parent, TModule& peer, TConstDepNodeRef) {
            auto& parentNodes = Modules.GetModuleNodeIds(parent.GetId());
            auto& peerNodes = Modules.GetModuleNodeIds(peer.GetId());
            Modules.GetNodeListStore().MergeLists(parentNodes.Tools, peerNodes.Tools);
        }
    };

    template<bool IsConst>
    struct TStartModulesSearchVisitor : TNoReentryVisitorBase<TEntryStats, TGraphIteratorStateItemBase<IsConst>> {
        using TBase = TNoReentryVisitorBase<TEntryStats, TGraphIteratorStateItemBase<IsConst>>;
        using TState = typename TBase::TState;

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }

            if (IsModule(state.Top())) {
                Modules.push_back(state.Top().Node());
                return false;
            }

            return true;
        }

        TVector<TDepGraph::TAnyNodeRef<IsConst>> Modules;
    };

    struct TMineStartModulesVisitor : public TNoReentryVisitorBase<TEntryStats, TGraphIteratorStateItemBase<true>> {
        using TBase = TNoReentryVisitorBase<TEntryStats, TGraphIteratorStateItemBase<true>>;
        using TState = typename TBase::TState;

        TMineStartModulesVisitor(const TVector<TTarget>& startTargets)
        {
            for (const auto& target : startTargets) {
                StartTargets.insert(target);
            }
        }

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }

            if (IsModule(state.Top())) {
                const TStateItem& parent = *state.Parent();
                Y_ASSERT(IsDirType(parent.Node()->NodeType));
                auto dirIt = StartTargets.find(parent.Node().Id());
                if (dirIt != StartTargets.end()) {
                    NewStartTargets.emplace_back(state.Top().Node().Id(), dirIt->AllFlags);
                    NewStartTargets.back().IsModuleTarget = true;
                }
                return false;
            }

            return true;
        }

        THashSet<TTarget> StartTargets;
        TVector<TTarget> NewStartTargets;
    };
}

TVarStr& TModuleRestorer::AddPath(TYVar& var, const TStringBuf& what) {
    TVarStr path(what);
    path.IsPathResolved = true;
    path.IsYPath = false;
    var.push_back(std::move(path));
    return var.back();
}

void TModuleRestorer::MineModuleDirs() {
    if (Module->IsDirsComplete()) {
        return;
    }

    for (const auto dep : Node.Edges()) {
        if (IsPeerdirDep(dep)) {
            const TConstDepNodeRef depNode = dep.To();
            Module->Peers.Push(TDepGraph::GetFileName(depNode));
        }
    }

    Module->SetDirsComplete();
}

void TModuleRestorer::MinePeers() {
    TTransitivePeersCollector collector{Context};
    TDirectReachablePeerCollectingVisitor visitor{collector};
    IterateAll(Node, visitor);
}

void TModuleRestorer::MineGlobVars() {
    Y_ASSERT(Module);
    if (Module->IsGlobVarsComplete()) {
        return;
    }
    TMineFilesGroupVarsVisitor visitor;
    IterateAll(Node, visitor);
    for (const auto& [varname, files]: visitor.GetMinedData()) {
        auto& var = Module->Vars[varname];
        var.clear();
        for (TNodeId fileId: files) {
            TFileView fileView = Context.Graph.GetFileName(Context.Graph.Get(fileId));
            TVarStr realPath{Context.Conf.RealPath(Context.Graph.Names().FileConf.ResolveLink(fileView))};
            realPath.IsPathResolved = true;
            realPath.IsYPath = false;
            var.push_back(std::move(realPath));
        }
        var.IsReservedName = true;
    }
    Module->SetGlobVarsComplete();
}

void TModuleRestorer::MineGlobalVars() {
    TGlobalVarsCollector collector{Context};
    TDirectReachablePeerCollectingVisitor visitor{collector};
    IterateAll(Node, visitor);
}

bool TModuleRestorer::IsFakeModule(ui32 elemId) const {
    return Context.Modules.Get(elemId)->IsFakeModule();
}

void TModuleRestorer::UpdateLocalVarsFromModule(TVars& vars, const TBuildConfiguration& conf, bool moduleUsesPeers) {
    MinePeers();
    MineGlobVars();

    for (auto lang : conf.LangsRequireBuildAndSrcRoots) {
        TLangId langId = NLanguages::GetLanguageId(lang);
        Y_ASSERT(langId != TModuleIncDirs::BAD_LANG);
        auto&& includeVarName = TModuleIncDirs::GetIncludeVarName(langId);
        AddPath(vars[includeVarName], conf.BuildRoot.c_str());
        AddPath(vars[includeVarName], conf.SourceRoot.c_str());
    }

    // This code uses cache-accelerated RealPath

    for (const auto& [langId, dirs] : Module->IncDirs.GetAll()) {
        const bool nonPathAddincls = NLanguages::GetLanguageAddinclsAreNonPaths(langId);

        auto&& varName = TModuleIncDirs::GetIncludeVarName(langId);
        for (const auto& incl : dirs.Get()) {
            if (nonPathAddincls) {
                AddPath(vars[varName], incl.CutAllTypes());
            } else {
                AddPath(vars[varName], conf.RealPath(incl));
            }
        }
    }

    if (Module->GetAttrs().RequireDepManagement) {
        Y_ASSERT(Module->IsDependencyManagementApplied());
        const auto lists = Context.Modules.GetModuleNodeLists(Module->GetId());
        vars["MANAGED_PEERS_CLOSURE"];
        vars["MANAGED_PEERS"];
        for (TNodeId peer: lists.UniqPeers()) {
            AddPath(vars["MANAGED_PEERS_CLOSURE"], conf.RealPath(Context.Graph.GetFileName(Context.Graph.Get(peer))));
        }
        for (TNodeId peer: lists.ManagedDirectPeers()) {
            AddPath(vars["MANAGED_PEERS"], conf.RealPath(Context.Graph.GetFileName(Context.Graph.Get(peer))));
        }
        if (const auto* dmInfo = Context.Modules.FindExtraDependencyManagementInfo(Module->GetId())) {
            auto& var = vars["APPLIED_EXCLUDES"];
            for (TNodeId excl: dmInfo->AppliedExcludes) {
                auto* exclMod = Context.Modules.Get(Context.Graph.Get(excl)->ElemId);
                Y_ASSERT(exclMod);
                AddPath(var, conf.RealPath(exclMod->GetDir().GetTargetStr()));
            }
        }
    }

    const auto& usedIncludesByLanguage = Module->IncDirs.GetAllUsed();
    for (size_t lang = 0; lang < usedIncludesByLanguage.size(); lang++) {
        vars.try_emplace(TModuleIncDirs::GetIncludeVarName(static_cast<TLangId>(lang)));
    }

    if (moduleUsesPeers) {
        TString prefix(NDetail::TReserveTag{32});
        auto modLists = Context.Modules.GetModuleNodeLists(Module->GetId());

        for (const auto& glSrcId : modLists.GlobalSrcsIds()) {
            AddPath(vars["SRCS_GLOBAL"], conf.RealPath(Context.Graph.GetFileName(Context.Graph.Get(glSrcId))));
        }
        TUniqVector<TString> lateOuts;
        bool usePeersLateOuts = Module->GetAttrs().UsePeersLateOuts;
        for (const auto& peer : modLists.UniqPeers()) {
            const auto peerRef = Context.Graph[peer];
            ui32 elemId = peerRef->ElemId;
            if (usePeersLateOuts) {
                for (const auto& lateOut : Context.Modules.GetModuleLateOuts(elemId)) {
                    lateOuts.Push(lateOut);
                }
            }

            if (IsFakeModule(elemId)) {
                continue;
            }
            const auto peerPath = conf.RealPath(Context.Graph.GetFileName(peerRef));

            prefix.clear();
            const auto peerModule = Context.Modules.Get(elemId);
            if (peerModule->IsCompleteTarget()) {
                prefix += ",complete";
            }
            if (peerModule->IsFinalTarget()) {
                prefix += ",final";
            }
            if (modLists.LocalPeers().contains(peer)) {
                prefix += ",local";
            }
            (prefix += ",") += peerModule->GetTag();

            auto& varStr = AddPath(vars["PEERS"], prefix + peerPath);
            varStr.HasPeerDirTags = true;
        }
        if (usePeersLateOuts) {
            vars["PEERS_LATE_OUTS"];
            for (auto& lateOut : lateOuts) {
                AddPath(vars["PEERS_LATE_OUTS"], lateOut);
            }
        }
    }

    for (const auto& [name, value]: Module->Vars) {
        if (value.IsReservedName) {
            vars[name] = value;
        }
    }
}

void TModuleRestorer::UpdateGlobalVarsFromModule(TVars& vars) {
    MineGlobalVars();
    auto appendModVars = [&] (ui32 modId, bool uniq) {
        const TVars& globalVars = Context.Modules.GetGlobalVars(modId).GetVars();
        for (auto& [name, val] : globalVars) {
            if (uniq) {
                vars[name].AppendUnique(val);
            } else {
                vars[name].Append(val);
            }
        }
    };
    appendModVars(Module->GetId(), false);

    if (Module->GetAttrs().RequireDepManagement) {
        const auto& managedPeers = Context.Modules.GetModuleNodeLists(Module->GetId()).UniqPeers();
        for (TNodeId peer: managedPeers) {
            appendModVars(Context.Graph[peer]->ElemId, true);
        }
        if (const auto* dmInfo = Context.Modules.FindExtraDependencyManagementInfo(Module->GetId())) {
            for (const auto& [name, _]: Context.Modules.GetGlobalVars(Module->GetId()).GetVars()) {
                vars["EXCLUDED_" + name] = {};
            }
            for (TNodeId exclude: dmInfo->AppliedExcludes) {
                const TVars& excludeVars = Context.Modules.GetGlobalVars(Context.Graph[exclude]->ElemId).GetVars();
                for (const auto& [name, val]: excludeVars) {
                    vars["EXCLUDED_" + name].AppendUnique(val);
                }
            }
        }
    }
}

TModule* TModuleRestorer::RestoreModule() {
    Module = &InitModule(Context.Modules, Context.Conf.CommandConf, Node);
    Module->SetAccessed();
    MineModuleDirs();
    return Module;
}

void TModuleRestorer::GetModuleDepIds(THashSet<TNodeId>& ids) {
    MinePeers();

    const auto modLists = Context.Modules.GetModuleNodeLists(Module->GetId());
    const auto& modUniquePeers = modLists.UniqPeers();
    ids.insert(modUniquePeers.begin(), modUniquePeers.end());
    ids.insert(modLists.DepCommandIds().begin(), modLists.DepCommandIds().end());
}

const TUniqVector<TNodeId>& TModuleRestorer::GetPeers() {
    if (!Module->GetAttrs().RequireDepManagement) {
        MinePeers();
    } else {
        Y_ASSERT(Module->IsPeersComplete());
    }
    return Context.Modules.GetModuleNodeLists(Module->GetId()).UniqPeers();
}

const TUniqVector<TNodeId>& TModuleRestorer::GetTools() {
    if (!Module->IsToolsComplete()) {
        TMineToolsVisitor visitor{Context.Modules};
        IterateAll(Node, visitor);
    }
    return Context.Modules.GetModuleNodeLists(Module->GetId()).Tools();
}

void TModuleRestorer::GetPeerDirIds(THashSet<TNodeId>& peerDirIds) {
    MinePeers();

    const auto& uniqPeers = Context.Modules.GetModuleNodeLists(Module->GetId()).UniqPeers();
    for (const auto& peer : uniqPeers) {
        TModule* module = Context.Modules.Get(Context.Graph[peer]->ElemId);
        Y_ASSERT(module);
        peerDirIds.insert(Context.Graph.GetFileNode(module->GetDir()).Id());
    }
}

const TUniqVector<TNodeId>& TModuleRestorer::GetGlobalSrcsIds() {
    MinePeers();
    return Context.Modules.GetModuleNodeLists(Module->GetId()).GlobalSrcsIds();
}

TModule* TModuleRestorer::GetModule() {
    return Module;
}

const TModule* TModuleRestorer::GetModule() const {
    return Module;
}

TVector<TConstDepNodeRef> GetStartModules(const TDepGraph& graph, const THashSet<TNodeId>& startDirs) {
    TStartModulesSearchVisitor<true> visitor;
    IterateAll(graph, startDirs, visitor);
    return visitor.Modules;
}

TVector<TConstDepNodeRef> GetStartModules(const TDepGraph& graph, const TVector<TTarget>& startTargets) {
    TStartModulesSearchVisitor<true> visitor;
    IterateAll(graph, startTargets, visitor, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });
    return visitor.Modules;
}

TVector<TTarget> GetStartTargetsModules(const TDepGraph& graph, const TVector<TTarget>& startTargets) {
    TMineStartModulesVisitor visitor(startTargets);
    IterateAll(graph, startTargets, visitor, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });
    return visitor.NewStartTargets;
}

TVector<TDepNodeRef> GetStartModules(TDepGraph& graph, const TVector<TTarget>& startTargets) {
    TStartModulesSearchVisitor<false> visitor;
    IterateAll(graph, startTargets, visitor, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });
    return visitor.Modules;
}

TModule& InitModule(TModules& modules, const TVars& commandConf, const TConstDepNodeRef& node) {
    ui32 elemId = node->ElemId;
    EMakeNodeType nodeType = node->NodeType;
    TModule* modPtr = modules.Get(elemId);

    Y_ASSERT(modPtr && modPtr->IsCommitted() && modPtr->GetNodeType() == nodeType);
    TModule& module = *modPtr;
    if (!module.IsInitComplete()) {
        Y_ASSERT(!module.Vars.Base || module.Vars.Base == &commandConf);
        module.Vars.Base = &commandConf;
        module.NotifyInitComplete();
    }

    return module;
}
