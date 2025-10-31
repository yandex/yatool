#include "sem_graph.h"
#include "dump_info.h"
#include "exec.h"
#include "mkcmd.h"
#include "module_store.h"
#include "flat_json_graph.h"
#include "json_subst.h"
#include "tools_miner.h"
#include "prop_names.h"

#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/managed_deps_iter.h>

#include <util/generic/stack.h>
#include <util/generic/overloaded.h>

template<>
struct NFlatJsonGraph::TNodePropertyTriat<TSingleCmd> {
    static void Serialize(NJsonWriter::TBuf& to, TSingleCmd value) {
        to.BeginObject();
        to.WriteKey("sem");
        std::visit(TOverloaded{
            [&](const TString& cmd) {
                to.UnsafeWriteValue(cmd);
            },
            [&](const TVector<TString>& cmd) {
                to.BeginList();
                for (auto&& arg: cmd)
                    to.WriteString(arg);
                to.EndList();
            }
        }, value.CmdStr);
        if (!value.Cwd.empty()) {
            to.WriteKey("cwd");
            to.WriteString(value.Cwd);
        }
        to.EndObject();
    }
};

namespace {

    class TDumpInfoSem : public TDumpInfoEx {
    public:
        TDumpInfoSem(TRestoreContext restoreContext, TConstDepNodeRef node, bool structCmdDetected)
            : RestoreContext{restoreContext}
            , Node{node}
            , StructCmdDetected(structCmdDetected)
        {}

        void SetExtraValues(TVars& vars) override {
            if (!IsModuleType(Node->NodeType)) {
                return;
            }

            const auto* mod = RestoreContext.Modules.Get(Node->ElemId);
            Y_ASSERT(mod);

            vars.SetValue("FAKE_MODULE", mod->IsFakeModule() ? "yes" : "no");

            SetAdincls(vars, *mod);
            SetGlobalVars(vars);
            SetSrcHeaders(vars);
        }

    private:
        void SetGlobalVars(TVars& vars) const {
            for (const auto& dep: Node.Edges()) {
                if (!IsBuildCmdInclusion(dep)) {
                    continue;
                }
                const auto& varStr = RestoreContext.Graph.GetCmdName(dep.To()).GetStr();
                const TStringBuf varname = GetCmdName(varStr);
                const TStringBuf varval = GetCmdValue(varStr);
                auto& var = vars[varname + TString("_RAW")];
                var.push_back(varval);
                if (StructCmdDetected)
                    var.back().StructCmdForVars = true;
            }
        }

        void SetAdincls(TVars& vars, const TModule& mod) const {
            for (size_t pos = 0; pos < mod.IncDirs.GetAllUsed().size(); ++pos) {
                const auto lang = static_cast<TLangId>(pos);
                auto& ownedAddincls = vars[mod.IncDirs.GetIncludeVarName(lang) + "_OWNED"];
                auto& globalAddincls = vars[mod.IncDirs.GetIncludeVarName(lang) + "_GLOBAL"];
                for (const auto& dir: mod.IncDirs.GetOwned(lang)) {
                    ownedAddincls.push_back({dir.GetTargetStr(), false, true});
                }
                for (const auto& dir: mod.IncDirs.GetGlobal(lang)) {
                    globalAddincls.push_back({dir.GetTargetStr(), false, true});
                }
            }
        }

        void SetSrcHeaders(TVars& vars) const {
            auto &var = vars["MODULE_EXPLICIT_HEADERS"];
            for (const auto& dep: Node.Edges()) {
                if (!IsModeHeaderAlikeSrcDep(dep))
                    continue;

                var.push_back({TDepGraph::GetFileName(dep.To()).GetTargetStr(), false, true});
            }
        }

        static bool IsModeHeaderAlikeSrcDep(TConstDepRef dep) {
            const auto toNoreType = dep.To()->NodeType;
            return IsModuleType(dep.From()->NodeType) && dep.Value() == EDT_Search && (toNoreType == EMNT_File || toNoreType == EMNT_NonParsedFile);
        }

    private:
        TRestoreContext RestoreContext;
        TConstDepNodeRef Node;
        bool StructCmdDetected;
    };

    class TSemFormatter: public TJsonCmdAcceptor {
    public:
        TVector<TSingleCmd>&& TakeCommands() && noexcept {return std::move(FormattedCommands);}

    protected:
        void OnCmdFinished(const TVector<TSingleCmd>& commands, TCommandInfo& cmdInfo[[maybe_unused]], const TVars& vars[[maybe_unused]]) override {
            FormattedCommands = commands;
        }

    private:
        TVector<TSingleCmd> FormattedCommands;
    };

    TVector<TSingleCmd> FormatCmd(
        const TRestoreContext& restoreContext,
        const TCommands& commands,
        TNodeId nodeId,
        TNodeId modId,
        TDumpInfoSem& semInfoProvider,
        TMakeModuleStates& modulesStatesCache
    ) {
        TSemFormatter formatter{};
        TMakeCommand mkcmd{modulesStatesCache, restoreContext, commands, nullptr, &restoreContext.Conf.CommandConf};
        mkcmd.CmdInfo.MkCmdAcceptor = formatter.GetAcceptor();
        mkcmd.GetFromGraph(nodeId, modId, &semInfoProvider);
        return std::move(formatter).TakeCommands();
    }

    class SemGraphRenderVisitor: public TManagedPeerConstVisitor<> {
    private:
        struct ModInfo {
            TNodeId ModNodeId;
            ui32 GlobalLibId;
            THashSet<TNodeId> TransitiveOnlyPeersUnderDM;
        };

    public:
        using TBase = TManagedPeerConstVisitor<>;
        using typename TBase::TState;

        SemGraphRenderVisitor(
            const TRestoreContext& restoreContext,
            const TCommands& commands,
            THashSet<TNodeId> startDirs,
            const THashSet<TTarget>& modStartTargets,
            IOutputStream& out
        )
            : TBase{restoreContext}
            , Commands{commands}
            , JsonWriter{out}
            , StartDirs{std::move(startDirs)}
            , ModStartTargets{modStartTargets}
            , ModulesStatesCache{restoreContext.Conf, restoreContext.Graph, restoreContext.Modules}
        {}

        bool AcceptDep(TState& state) {
            if (!TBase::AcceptDep(state)) {
                return false;
            }
            const auto& dep = state.NextDep();
            if (IsPropertyDep(dep)) {
                return false;
            }
            if (!state.HasIncomingDep() && IsDirToModuleDep(dep) && !ModStartTargets.contains(TTarget{dep.To().Id()})) {
                return false;
            }
            if (*dep == EDT_OutTogetherBack) {
                return false;
            }
            if (!IsIncludeFileDep(dep)) {
                return true;
            }
            // Check IsLink and resolve it only if ready return false
            return TFileId::Create(dep.From()->ElemId).IsLink()
                && NPath::GetType(RestoreContext.Graph.GetFileName(dep.From()->ElemId).GetTargetStr()) == NPath::ERoot::Build;
        }

        bool Enter(TState& state) {
            if (!TBase::Enter(state)) {
                return false;
            }
            const auto& topNode = state.TopNode();
            TModule* mod = nullptr;
            if (IsModuleType(topNode->NodeType)) {
                mod = RestoreContext.Modules.Get(topNode->ElemId);
                Y_ASSERT(mod);
                const TVector<TNodeId>* managedPeersClosure = nullptr;
                THashSet<TNodeId> transitiveOnlyPeersUnderDM;
                if (mod->IsDependencyManagementApplied()) {
                    managedPeersClosure = &RestoreContext.Modules.GetModuleNodeLists(mod->GetId()).UniqPeers().Data();
                    const auto& managedDirectPeers = RestoreContext.Modules.GetModuleNodeLists(mod->GetId()).ManagedDirectPeers();
                    for (TNodeId peer: *managedPeersClosure) {
                        if (!managedDirectPeers.has(peer)) {
                            transitiveOnlyPeersUnderDM.insert(peer);
                        }
                    }
                }
                ModulesStack.push({
                    .ModNodeId = topNode.Id(),
                    .GlobalLibId = mod->GetAttrs().UseGlobalCmd ? mod->GetGlobalLibId() : 0,
                    .TransitiveOnlyPeersUnderDM = std::move(transitiveOnlyPeersUnderDM),
                });
                if (managedPeersClosure && !managedPeersClosure->empty()) {
                    // Iterate all peers closure before add node ids, else peers of contrib peers will not add to sem graph,
                    // and as result node ids below will to point to absent nodes
                    IterateAll(RestoreContext.Graph, *managedPeersClosure, state, *this);
                }
            }

            if (UseFileId(topNode->NodeType)) {
                auto node = JsonWriter.AddNode(topNode);
                if (topNode->NodeType == EMNT_Directory && StartDirs.contains(topNode.Id())) {
                    node.AddProp("Tag", "StartDir");
                }
                if (IsOutputType(topNode->NodeType) && !AnyOf(topNode.Edges(), [](const auto& dep) {return *dep == EDT_OutTogether;})
                    // Some link may has no commands, skip render semantics for it, else "No pattern for node" error
                    && (!TFileConf::IsLink(topNode->ElemId) || AnyOf(topNode.Edges(), [](const auto& dep) {return *dep == EDT_BuildFrom || dep.To()->NodeType == EMNT_BuildCommand;}))
                ) {
                    bool structCmdDetected = false;
                    for (const auto& dep : topNode.Edges()) {
                        if (*dep == EDT_BuildCommand && dep.To()->NodeType == EMNT_BuildCommand) {
                            structCmdDetected = TVersionedCmdId(dep.To()->ElemId).IsNewFormat();
                            break;
                        }
                    }
                    Y_ASSERT(!ModulesStack.empty());
                    const auto& modinfo = ModulesStack.top();
                    TDumpInfoSem semVarsProvider{
                        RestoreContext,
                        modinfo.GlobalLibId == topNode->ElemId ? RestoreContext.Graph[modinfo.ModNodeId] : topNode,
                        structCmdDetected
                    };
                    static const TSingleCmd::TCmdStr CMD_IGNORED = TStringBuilder() << "[\"" << TModuleConf::SEM_IGNORED << "\"]";
                    static const TSingleCmd::TCmdStr CMD_IGNORED_NEW = TVector<TString>{TString{TModuleConf::SEM_IGNORED}};
                    auto sem = FormatCmd(RestoreContext, Commands, topNode.Id(), ModulesStack.top().ModNodeId, semVarsProvider, ModulesStatesCache);
                    bool ignored = AnyOf(sem, [](const TSingleCmd& cmd) {
                        return cmd.CmdStr == CMD_IGNORED || cmd.CmdStr == CMD_IGNORED_NEW;
                    });
                    if (!ignored && mod && mod->IsSemIgnore()) {
                        ignored = true;
                        sem.emplace_back(CMD_IGNORED); // generate IGNORED in semantic by module SemIgnore flag
                    }
                    node.AddProp("semantics", sem);
                    if (ignored) {
                        return false;
                    }
                    const auto tools = ToolMiner.MineTools(topNode);
                    if (!tools.empty()) {
                        node.AddProp("Tools", tools);
                    }
                    if (mod && RestoreContext.Conf.ShouldTraverseDepsTests()) {
                        auto tests = RenderTests(
                            RestoreContext.Graph.Names().FileConf.ConstructLink(ELinkType::ELT_MKF, mod->GetMakefile())
                        );
                        if (!tests.empty()) {
                            node.AddProp("Tests", tests);
                        }
                    }
                }
            }
            return true;
        }

        void Leave(TState& state) {
            const auto& topNode = state.TopNode();
            if (!ModulesStack.empty() && topNode.Id() == ModulesStack.top().ModNodeId) {
                auto mod = RestoreContext.Modules.Get(topNode->ElemId);
                auto transitiveOnlyPeersUnderDM = std::move(ModulesStack.top().TransitiveOnlyPeersUnderDM);
                ModulesStack.pop();
                if (!transitiveOnlyPeersUnderDM.empty()) {
                    for (const auto transitivePeerId: transitiveOnlyPeersUnderDM) {
                        auto peerNode = RestoreContext.Graph[transitivePeerId];
                        auto peerMod = RestoreContext.Modules.Get(peerNode->ElemId);
                        Y_ASSERT(peerMod);
                        // Sem graph use ElemId as id of nodes, use TModule->GetId()
                        auto node = JsonWriter.AddLink(mod->GetId(), mod->GetNodeType(), peerMod->GetId(), peerMod->GetNodeType(), EDepType::EDT_BuildFrom, NFlatJsonGraph::EIDFormat::Simple);
                        AddExcludeProperty(node, topNode, peerNode, true); // generate Excludes attribute
                        AddIsClosureProperty(node); // IsClosure flag attribute
                    }
                }
            }
            TBase::Leave(state);
        }

        void Left(TState& state) {
            if (state.Top().IsDepAccepted()) {
                const auto& dep = state.Top().CurDep();
                if (UseFileId(dep.From()->NodeType) && UseFileId(dep.To()->NodeType)) {
                    auto node = JsonWriter.AddLink(dep);
                    AddExcludeProperty(node, dep);
                }
            }
            TBase::Left(state);
        }

    private:
        TVector<ui32> RenderTests(TFileView mkf) {
            TVector<ui32> res;
            const auto node = RestoreContext.Graph.GetFileNode(mkf);
            for (const auto& dep: node.Edges()) {
                if (!IsMakeFilePropertyDep(dep.From()->NodeType, *dep, dep.To()->NodeType) || GetCmdName(RestoreContext.Graph.GetCmdName(dep.To()).GetStr()) != NProps::TEST_RECURSES) {
                    continue;
                }
                for (const auto &testdep: dep.To().Edges()) {
                    if (IsSearchDirDep(testdep)) {
                        res.push_back(testdep.To()->ElemId);
                    }
                }
            }
            return res;
        }

        void AddExcludeProperty(NFlatJsonGraph::TNodeWriter node, TConstDepRef dep) {
            return AddExcludeProperty(node, dep.From(), dep.To(), IsDirectPeerdirDep(dep));
        }

        void AddExcludeProperty(NFlatJsonGraph::TNodeWriter node, TConstDepNodeRef fromNode, TConstDepNodeRef toNode, bool isDirectPeerdirDep) {
            auto excludeNodeIds = ComputeExcludeNodeIds(fromNode, toNode, isDirectPeerdirDep);
            if (!excludeNodeIds.empty()) {
                // To dependences attribute name after ':' add info about type ([] - array) and element type (NodeId)
                // P.S. In sem-graph Id of node is ElemId from dep-graph, that is why typename of element is "NodeId"
                node.AddProp("Excludes:[NodeId]", TDepGraph::NodeToElemIds(RestoreContext.Graph, excludeNodeIds));
            }
        }

        void AddIsClosureProperty(NFlatJsonGraph::TNodeWriter node) {
            node.AddProp("IsClosure:bool", true);
        }

        THashSet<TNodeId> ComputeExcludeNodeIds(TConstDepNodeRef fromNode, TConstDepNodeRef toNode, bool isDirectPeerdirDep) {
            if (!isDirectPeerdirDep) {
                return {};
            }
            const TModule* fromMod = RestoreContext.Modules.Get(fromNode->ElemId);
            const TModule* toMod = RestoreContext.Modules.Get(toNode->ElemId);
            if (!fromMod->IsDependencyManagementApplied() || !toMod->IsDependencyManagementApplied()) {
                // Excludes can compute only if both modules under DM
                return {};
            }
            const auto& toManagedPeersClosure = RestoreContext.Modules.GetModuleNodeLists(toMod->GetId()).UniqPeers().Data();
            if (toManagedPeersClosure.empty()) {
                // Empty closure, nothing can be excluded
                return {};
            }
            THashSet<TNodeId> excludeNodeIds(toManagedPeersClosure.begin(), toManagedPeersClosure.end());
            const auto& fromManagedPeersClosure = RestoreContext.Modules.GetModuleNodeLists(fromMod->GetId()).UniqPeers().Data();
            // Remove from TO (library) managed peers closure all FROM (program) managed peers closure ids
            // As result, after subtracting sets, we found all nodes for exclude by this dependency
            for (auto nodeId : fromManagedPeersClosure) {
                if (auto nodeIt = excludeNodeIds.find(nodeId); nodeIt != excludeNodeIds.end()) {
                    excludeNodeIds.erase(nodeIt);
                }
            }
            return excludeNodeIds;
        }

    private:
        const TCommands& Commands;
        NFlatJsonGraph::TWriter JsonWriter;
        TStack<ModInfo> ModulesStack;
        THashSet<TNodeId> StartDirs;
        const THashSet<TTarget>& ModStartTargets;
        TToolMiner ToolMiner;
        TMakeModuleSequentialStates ModulesStatesCache;
    };

}

void RenderSemGraph(
    IOutputStream& out,
    TRestoreContext restoreContext,
    const TCommands& commands,
    TTraverseStartsContext startsContext
) {
    THashSet<TNodeId> startDirs;
    for (const auto& tgt : startsContext.StartTargets) {
        if (!tgt.IsNonDirTarget && !tgt.IsDepTestTarget) {
            startDirs.insert(tgt.Id);
        }
    }
    SemGraphRenderVisitor visitor{restoreContext, commands, std::move(startDirs), startsContext.ModuleStartTargets, out};
    IterateAll(restoreContext.Graph, startsContext.StartTargets, visitor, [](const TTarget& t) -> bool { return !t.IsModuleTarget; });
}
