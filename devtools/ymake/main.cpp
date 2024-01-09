#include "build_result.h"
#include "conf.h"
#include "dependency_management.h"
#include "export_json.h"
#include "managed_deps_iter.h"
#include "node_printer.h"
#include "prop_names.h"
#include "propagate_change_flags.h"
#include "recurse_graph.h"
#include "sem_graph.h"
#include "transitive_requirements_check.h"
#include "ymake.h"

#include <devtools/ymake/compact_graph/dep_types.h>

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/loops.h>

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/diag/trace.ev.pb.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/progress_manager.h>

#include <devtools/ymake/yndex/yndex.h>

#include <library/cpp/json/writer/json.h>
#include <library/cpp/json/writer/json_value.h>

#include <util/datetime/uptime.h>
#include <util/folder/path.h>
#include <util/generic/fwd.h>
#include <util/generic/hash_set.h>
#include <util/generic/maybe.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/stream/output.h>
#include <util/string/cast.h>
#include <util/string/join.h>
#include <util/string/split.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

#include <fmt/format.h>

#include <new>

namespace {
    void MakeUnique(TVector<TTarget>& targets) {
        using Id = decltype(TTarget::Id);
        using size_type = TVector<TTarget>::size_type;
        TVector<TTarget> newTargets;
        THashMap<Id, size_type> Avail;
        for (const auto& t : targets) {
            auto [it, added] = Avail.try_emplace(t.Id, newTargets.size());
            if (added) {
                newTargets.push_back(t);
            } else {
                auto& newTarget = newTargets[it->second];
                newTarget.AllFlags |= t.AllFlags;
            }
        }
        targets = std::move(newTargets);
    }
}

TYMake::TYMake(TBuildConfiguration& conf)
    : Conf(conf)
    , Graph(Names)
    , RecurseGraph(Names)
    , Names(Conf, Conf, TimeStamps)
    , TimeStamps(Names)
    , IncParserManager(conf, Names)
    , Yndex(Conf.CommandDefinitions)
    , Modules(Names, conf.PeersRules, Conf)
{
    TimeStamps.StartSession();
    Diag()->Where.clear();
}

static void PrintAllFiles(const TYMake& ymake) {
    YInfo() << "All Files:" << Endl;
    const TDepGraph& graph = ymake.Graph;

    for (auto node : graph.Nodes()) {
        if (IsFileType(node->NodeType)) {
            TString nameWithCtx;
            graph.GetFileName(node).GetStr(nameWithCtx);
            YInfo() << "file: " << node->NodeType << ' ' << nameWithCtx << Endl;
        }
    }
}

static void PrintAllDirs(const TYMake& ymake) {
    YInfo() << "All Directories:" << Endl;
    const TDepGraph& graph = ymake.Graph;

    for (auto node : graph.Nodes()) {
        if (UseFileId(node->NodeType) && !IsFileType(node->NodeType)) {
            TStringBuf name = graph.GetFileName(node).GetTargetStr();
            YInfo() << "dir: " << node->NodeType << ' ' << name << Endl;
        }
    }
}

static void PrintFlatGraph(const TYMake& ymake) {
    YInfo() << "Flat graph dump:" << Endl;
    const TDepGraph& graph = ymake.Graph;

    for (auto node : graph.Nodes()) {
        TString name = (node->NodeType != EMNT_Deleted || node->ElemId != 0) ? graph.ToString(node) : "<Null>";
        Cout << node.Id() << ' ' << node->NodeType << ' ' << name << '(' << node->ElemId << ") - " << node.Edges().Total() << " deps:" << Endl;

        for (auto dep : node.Edges()) {
            Cout << "  "
                 << "[" << dep.Index() << "] "
                 << "-- " << *dep << " --> " << dep.To().Id() << ' ' << dep.To()->NodeType << ' ' << graph.ToString(dep.To()) << '(' << dep.To()->ElemId << ')' << Endl;
        }
    }
    if (ymake.Conf.DumpPretty)
        Cout << Endl;
}

namespace {
    class TBuildGraphScope {
         TUpdIter UpdIter;
         TGeneralParser Parser;
    public:
         explicit TBuildGraphScope(TYMake& yMake)
             : UpdIter(yMake)
             , Parser(yMake)
         {
             yMake.UpdIter = &UpdIter;
             yMake.Parser = &Parser;
         }
         ~TBuildGraphScope() {
              UpdIter.YMake.UpdIter = nullptr;
              UpdIter.YMake.Parser = nullptr;
         }
    };
}

static TMaybe<EBuildResult> ConfigureGraph(THolder<TYMake>& yMake) {
    FORCE_TRACE(U, NEvent::TStageStarted("Configure graph"));
    try {
        yMake->BuildDepGraph();
    } catch (const TNotImplemented& e) {
        TRACE(G, NEvent::TRebuildGraph(e.what()));
        YConfWarn(KnownBug) << "Graph needs to be rebuilt: " << e.what() << Endl;
        return TMaybe<EBuildResult>(BR_RETRYABLE_ERROR);
    } catch (const TInvalidGraph& e) {
        TRACE(G, NEvent::TRebuildGraph(e.what()));
        YConfWarn(KnownBug) << "Graph update failed: " << e.what() << Endl;
        yMake->UpdIter->DumpStackW();
        return TMaybe<EBuildResult>(BR_RETRYABLE_ERROR);
    } catch(const yexception& error) {
        YErr() << error.what() << Endl;
        return TMaybe<EBuildResult>(BR_FATAL_ERROR);
    }
    FORCE_TRACE(U, NEvent::TStageFinished("Configure graph"));
    return TMaybe<EBuildResult>();
}

static TMaybe<EBuildResult> StaticConfigureGraph(THolder<TYMake>& yMake) {
    FORCE_TRACE(U, NEvent::TStageStarted("Configure readonly graph"));
    TBuildConfiguration& conf = yMake->Conf;
    for (size_t i = 0; i < conf.StartDirs.size(); ++i) {
        TString curDir = NPath::ConstructPath(NPath::FromLocal(conf.StartDirs[i]), NPath::Source);
        ui64 node = yMake->Graph.Names().FileConf.GetIdNx(curDir);
        if (!node)
            continue;
        const TNodeId offs = yMake->Graph.GetFileNodeById(node).Id();
        if (offs)
            yMake->StartTargets.push_back(offs);
    }
    FORCE_TRACE(U, NEvent::TStageFinished("Configure readonly graph"));
    return yMake->StartTargets.size() ? TMaybe<EBuildResult>() : TMaybe<EBuildResult>(BR_CONFIGURE_FAILED);
}

void TYMake::BuildDepGraph() {
    UpdIter->RestorePropsToUse();

    TModule& rootModule = Modules.GetRootModule();
    TUniqVector<TNodeId> startDirs;
    for (size_t i = 0; i < Conf.StartDirs.size(); ++i) {
        TString curDir = NPath::ConstructPath(NPath::FromLocal(Conf.StartDirs[i]), NPath::Source);
        YDebug() << "Parsing dir " << curDir << Endl;
        const TNodeId nodeId = UpdIter->RecursiveAddStartTarget(EMNT_Directory, curDir, &rootModule);
        if (nodeId) {
            startDirs.Push(nodeId);
        }
    }
    if (Conf.ShouldTraverseDepsTests()) {
        // PEERDIRS of added tests which are not required by targets reachable from start targets are added to
        // the end of AllNodes collection as well as their tests. Thus the loop below acts similar to BFS: once
        // the loop is finished all required libraries and their tests are configured and added to the graph.
        //
        // IMPORTANT deque has pointerstability on Push_back but not iterator stability. Loop body triggers
        // some code which adds new items into AllNodes and range based for as well as any iterator based
        // loop leads to UB.
        for (size_t idx = 0; idx < UpdIter->RecurseQueue.GetAllNodes().size(); ++idx) {
            auto node = UpdIter->RecurseQueue.GetAllNodes()[idx];
            const auto* deps = UpdIter->RecurseQueue.GetDeps(node);
            if (!deps) {
                continue;
            }
            for (const auto& [to, dep] : *deps) {
                if (IsTestRecurseDep(node.NodeType, dep, to.NodeType)) {
                    UpdIter->RecursiveAddStartTarget(to.NodeType, Graph.GetFileName(to.ElemId).GetTargetStr(), &rootModule);
                }
            }
        }
    }

    UpdIter->DelayedSearchDirDeps.Flush(*Parser, Graph);
    Graph.RelocateNodes(Parser->RelocatedNodes);

    for (const auto& s : startDirs) {
        StartTargets.push_back(s);
    }
}

TNodeId TYMake::GetUserTarget(const TStringBuf& target) const {
    YDebug() << "Searching target '" << target << "'" << Endl;
    TString from = NPath::ConstructPath(target, NPath::Source);
    TNodeId nodeId = Graph.GetFileNode(from).Id();
    if (!nodeId) {
        from = NPath::ConstructPath(target, NPath::Build);
        nodeId = Graph.GetFileNode(from).Id();
    }
    if (!nodeId) {
        from = target;
        nodeId = Graph.GetFileNode(from).Id();
    }
    YDebug() << "Selected target '" << from << "' with id " << nodeId << Endl;
    return nodeId;
}

bool TYMake::InitTargets() {
    for (const TFsPath& target: Conf.Targets) {
        TNodeId offs = GetUserTarget(NPath::FromLocal(target));
        if (!offs) {
            YConfErr(UserErr) << "Target '" << target.c_str() << "' not found" << Endl;
            continue;
        }
        EMakeNodeType type = Graph.GetType(offs);
        if (type == EMNT_Directory) {
            //XXX: try to not iterate here
            for (auto& s : StartTargets) {
                if (offs == s.Id) {
                    s.IsUserTarget = true;
                    break;
                }
            }
            continue;
        }
        if (!IsOutputType(type)) {
            YWarn() << target.c_str() << " is not a buildable target. Skip it." << Endl;
            continue;
        }
        StartTargets.push_back(offs);
        StartTargets.back().IsUserTarget = true;
        StartTargets.back().IsNonDirTarget = true;
        HasNonDirTargets = true;
    }
    return !StartTargets.empty();
}

namespace {
    // This now applies only to multimodules.
    // TODO(spreis): generalize to all cases
    bool IsDependsFinalTarget(const TModules& Modules, ui32 modId) {
        const TModule* mod = Modules.Get(modId);
        Y_ASSERT(mod != nullptr);
        return mod && mod->IsFinalTarget(); // Shouldn't we allow also UNIONs here?
    }

    bool IsFakeModule(const TModules& Modules, ui32 modId) {
        const TModule* mod = Modules.Get(modId);
        Y_ASSERT(mod != nullptr);
        return mod && mod->IsFakeModule();
    }

    bool ValidateRecurseDep(TDepGraph& graph, const TDepTreeNode& node, const TDepTreeNode& depNode, const EDepType depType) {
        auto actualDepDirType = graph.GetNodeById(depNode)->NodeType;
        if (IsInvalidDir(actualDepDirType)) {
            const TFileView depDirName = graph.GetFileName(depNode);

            auto makefileName = NPath::SmartJoin(graph.GetFileName(node).GetTargetStr(), "ya.make");
            TScopedContext context(graph.Names().FileConf.Add(makefileName), makefileName, false);
            TStringBuf macro = depType == EDT_Include ? TStringBuf("RECURSE") : TStringBuf("DEPENDS");
            YConfErr(BadDir) << "[[alt1]]" << macro << "[[rst]] to " << (actualDepDirType == EMNT_NonProjDir ? "directory without ya.make: " : "missing directory: ") << depDirName << Endl;
            TRACE(P, NEvent::TInvalidRecurse(TString(depDirName.GetTargetStr())));
            return false;
        }

        return true;
    }

    class TModulesInfo {
    private:
        const TDepGraph& Graph;
        const TModules& Modules;

        THashMap<ui32, std::pair<ui32, ui32>> ModulesCountByDirId;

    public:
        TModulesInfo(const TDepGraph& graph, const TModules& modules)
            : Graph(graph)
            , Modules(modules)
        {}

        std::pair<ui32, ui32> Get(const TDepTreeNode& node) {
            Y_ASSERT(IsDirType(node.NodeType));
            auto it = ModulesCountByDirId.find(node.ElemId);
            if (it != ModulesCountByDirId.end()) {
                return it->second;
            }
            ui32 count = 0, allowedForDependsCount = 0;
            const auto nodeRef = Graph.GetNodeById(node);
            Y_ASSERT(nodeRef.IsValid());
            for (const auto& edge : nodeRef.Edges()) {
                if (node.NodeType == EMNT_Directory && edge.Value() == EDT_Include && IsModuleType(edge.To()->NodeType)) {
                    ++count;
                    if (IsDependsFinalTarget(Modules, edge.To()->ElemId)) {
                        // Should we also support UNION here?
                        ++allowedForDependsCount;
                    }
                }
            }
            auto result = std::make_pair(count, allowedForDependsCount);
            ModulesCountByDirId[node.ElemId] = result;
            return result;
        }
    };

    bool ValidateDependsDep(TDepGraph& graph, const TDepTreeNode& from, const TDepTreeNode& to, TModulesInfo& modulesInfo) {
        const auto& [count, allowedForDependsCount] = modulesInfo.Get(to);
        if (count == 0 || (count > 1 && allowedForDependsCount == 0)) {
            auto depDirName = graph.GetFileName(to).GetTargetStr();
            auto makefileName = NPath::SmartJoin(graph.GetFileName(from).GetTargetStr(), "ya.make");
            TScopedContext context(graph.Names().FileConf.Add(makefileName), makefileName, false);
            if (count == 0) {
                YConfErr(BadDir) << "[[alt1]]DEPENDS[[rst]] to directory without module: " << depDirName << Endl;
                TRACE(P, NEvent::TInvalidRecurse(TString(depDirName)));
            } else {
                YConfErr(BadDir) << "[[alt1]]DEPENDS[[rst]] on a multimodule without final targets is not allowed: " << depDirName << Endl;
            }
            return false;
        }
        return true;
    }
}

void TYMake::AddRecursesToStartTargets() {
    if (HasNonDirTargets) {
        return;
    }

    TNodesQueue pureRecursesQueue;
    for (const auto& node: StartTargets) {
        if (node.IsModuleTarget) {
            continue;
        }
        pureRecursesQueue.MarkReachable(Graph.Get(node).Value());
    }

    for (const auto& node : UpdIter->RecurseQueue.GetReachableNodes()) {
        const auto deps = UpdIter->RecurseQueue.GetDeps(node);
        if (deps) {
            for (const auto& [depNode, depType] : *deps) {
                if (IsPureRecurseDep(node.NodeType, depType, depNode.NodeType)) {
                    pureRecursesQueue.AddEdge(node, depNode, depType);
                }
            }
        }
    }

    // Deps tests are added after adding reachable node recurses intentionally
    // Deps tests should be added as requested targets but their recurses shouldn't.
    THashSet<ui32> depsTests;
    if (Conf.ShouldTraverseDepsTests()) {
        for (const auto& node : UpdIter->RecurseQueue.GetAllNodes()) {
            if (const auto deps = UpdIter->RecurseQueue.GetDeps(node)) {
                for (const auto& [depNode, depType] : *deps) {
                    if (IsTestRecurseDep(node.NodeType, depType, depNode.NodeType) && !pureRecursesQueue.IsReachable(depNode)) {
                        pureRecursesQueue.MarkReachable(depNode);
                        depsTests.insert(depNode.ElemId);
                    }
                }
            }
        }
    }

    TUniqContainerImpl<TDepTreeNode, TDepTreeNode, 16> depends;
    for (const auto& node : UpdIter->RecurseQueue.GetReachableNodes()) {
        const auto deps = UpdIter->RecurseQueue.GetDeps(node);
        if (deps) {
            for (const auto& [depNode, depType] : *deps) {
                if (pureRecursesQueue.IsReachable(node) && IsDependsDep(node.NodeType, depType, depNode.NodeType)) {
                    depends.Push(depNode);
                }
            }
        }
    }

    if (Conf.ShouldAddPeerdirsGenTests()) {
        for (const auto& node : UpdIter->RecurseQueue.GetAllNodes()) {
            const auto deps = UpdIter->RecurseQueue.GetDeps(node);
            if (!deps) {
                continue;
            }
            for (const auto& [depNode, depType] : *deps) {
                if (IsDependsDep(node.NodeType, depType, depNode.NodeType)) {
                    depends.Push(depNode);
                }
            }
        }
    }

    const auto iterateAllGraph = Diag()->ShowAllBadRecurses;
    TModulesInfo modulesInfo(Graph, Modules);
    const auto& nodes = iterateAllGraph ? UpdIter->RecurseQueue.GetAllNodes() : UpdIter->RecurseQueue.GetReachableNodes();
    for (const auto& node : nodes) {
        const auto deps = UpdIter->RecurseQueue.GetDeps(node);
        if (deps) {
            THashSet<std::pair<TDepTreeNode, EDepType>> checked;
            for (const auto& dep : *deps) {
                if (!checked.insert(dep).second) {
                    continue;
                }
                const auto& [depNode, depType] = dep;
                if (!ValidateRecurseDep(Graph, node, depNode, depType)) {
                    continue;
                }
                if (IsDependsDep(node.NodeType, depType, depNode.NodeType) && UpdIter->RecurseQueue.IsReachable(depNode)) {
                    ValidateDependsDep(Graph, node, depNode, modulesInfo);
                }
            }
        }
    }

    while (!pureRecursesQueue.Empty()) {
        const auto node = pureRecursesQueue.GetFront();
        const auto commitedNode = Graph.GetNodeById(node);
        pureRecursesQueue.Pop();
        if (IsInvalidDir(commitedNode->NodeType)) {
            continue;
        }
        auto nodeId = commitedNode.Id();
        YDIAG(ShowRecurses) << "Recursed target " << Graph.GetFileName(Graph.Get(nodeId)) << Endl;
        StartTargets.push_back(nodeId);
        StartTargets.back().IsRecurseTarget = true;
        if (depsTests.contains(node.ElemId)) {
            StartTargets.back().IsDepTestTarget = true;
        }
        if (depends.has(node)) {
            StartTargets.back().IsDependsTarget = true;
        }
    }

    for (const auto& node : depends) {
        const auto commitedNode = Graph.GetNodeById(node);
        if (!IsInvalidDir(commitedNode->NodeType) && !pureRecursesQueue.IsReachable(node)) {
            StartTargets.push_back(commitedNode.Id());
            StartTargets.back().IsDependsTarget = true;
        }
    }
}

void TYMake::CreateRecurseGraph() {
    TMineRecurseVisitor mineVisitor(Graph, RecurseGraph);
    IterateAll(Graph, StartTargets, mineVisitor, [](TTarget target){ return !target.IsModuleTarget; });

    TFilterRecurseVisitor filterVisitor(Graph, StartTargets, RecurseGraph);
    IterateAll(RecurseGraph, filterVisitor);

    for (auto node : filterVisitor.GetFilteredNodes()) {
        RecurseGraph.DeleteNode(node);
    }

    RecurseGraph.Compact();

    RecurseStartTargets.clear();
    for (auto target : StartTargets) {
        if (!target.IsUserTarget || target.IsModuleTarget) {
            continue;
        }

        ui32 targetElemId = Graph.Get(target.Id)->ElemId;
        auto node = RecurseGraph.GetFileNodeById(targetElemId);
        if (node.IsValid()) {
            RecurseStartTargets.push_back(node.Id());
        }
    }
}

void TYMake::AddModulesToStartTargets() {
    TVector<TStringBuf> buildLanguages;
    const char* filterLangs = "BUILD_LANGUAGES";
    auto langCmd = Conf.CommandConf.Get1(filterLangs);
    if (langCmd) {
        auto languagesStr = GetCmdValue(langCmd);
        Split(languagesStr, " ", buildLanguages);
    }
    auto filter = [&buildLanguages](const TModule& mod) {
        if (!mod.IsStartTarget()) {
            return false;
        }
        if (buildLanguages.empty()) {
            return true;
        }
        auto langIt = std::find(buildLanguages.begin(), buildLanguages.end(), mod.GetLang());
        return langIt != buildLanguages.end();
    };
    auto startTargetsModules = GetStartTargetsModules(Graph, StartTargets);
    for (const auto& target : startTargetsModules) {
        auto mod = Modules.Get(Graph.GetFileName(Graph.Get(target.Id)).GetElemId());
        Y_ASSERT(mod);
        if (target.IsDependsTarget || filter(*mod)) {
            StartTargets.push_back(target);
            ModuleStartTargets.insert(target);
        }
    }

    CreateRecurseGraph();
}

// We are going to compute:
//   * DEPENDS -> module closure;
//   * PEERDIR -> UNION closure for DEPENDS -> UNION statements,
// In other words we want to add some extra DEPENDS statements
// Suppose we have EXECTEST --DEPENDS-> UNION(union_outer) --PEERDIR-> UNION(union_middle) --PEERDIR-> UNION(union_inner)
// so in addition to DEPENDS(union_outer) we have to add DEPENDS(union_middle) DEPENDS(union_inner) as well.
// Below is a part of the graph which represents the above structure:
// [0 of 0] Dep: EDT_Search, Type: EMNT_Directory, Id: 1, Name: $S/junk/snermolaev/depends_union
//  . . .
//  [3 of 3] Dep: EDT_BuildFrom, Type: EMNT_Directory, Id: 4, Name: $S/junk/snermolaev/union_outer
//   . . .
//   [2 of 2] Dep: EDT_Include, Type: EMNT_Bundle, Id: 7, Name: $B/junk/snermolaev/union_outer/junk-snermolaev-union_outer.pkg.fake
//    [1 of 7] Dep: EDT_Include, Type: EMNT_Directory, Id: 8, Name: $S/junk/snermolaev/union_middle
//     . . .
//     [2 of 2] Dep: EDT_Include, Type: EMNT_Bundle, Id: 10, Name: $B/junk/snermolaev/union_middle/junk-snermolaev-union_middle.pkg.fake
//      [1 of 7] Dep: EDT_Include, Type: EMNT_Directory, Id: 11, Name: $S/junk/snermolaev/union_inner
//       . . .
//       [2 of 2] Dep: EDT_Include, Type: EMNT_Bundle, Id: 13, Name: $B/junk/snermolaev/union_inner/junk-snermolaev-union_inner.pkg.fake
class TDependsToModulesCollector: public TNoReentryStatsConstVisitor<> {
    using TBase = TNoReentryStatsConstVisitor<>;
public:
    TDependsToModulesCollector(const TRestoreContext& restoreContext, TDependsToModulesClosure& closure)
        : RestoreContext(restoreContext)
        , Closure(closure)
    {
    }

    void Reset() {
        TBase::Reset();
        IsMultimodule = false;
    }

    bool Enter(TState& state) {
        if (!state.HasIncomingDep()) {
            const auto dependsNode = state.TopNode();
            if (dependsNode->NodeType != EMNT_Directory) {
                return false;
            }
            const auto dirName = RestoreContext.Graph.GetFileName(dependsNode).CutType();
            auto [it, added] = Closure.try_emplace(dirName, TVector<TNodeId>());
            if (!added) {
                return false;
            }
            Iterator = it;
        }
        return TBase::Enter(state);
    }

    bool AcceptDep(TState& state) {
        const auto dep = state.NextDep();
        if (*dep == EDT_Property) {
            if (dep.To()->NodeType == EMNT_Property && GetPropertyName(TDepGraph::GetCmdName(dep.To()).GetStr()) == MULTIMODULE_PROP_NAME) {
                IsMultimodule = true;
            }
        }

        // TODO(DEVTOOLS-8767) IsModuleWithManageablePeers check must be removed!!!
        if ((*dep != EDT_Include && !IsModuleWithManageablePeers(dep.From())) || !IsModuleType(dep.To()->NodeType) || !IsReachableManagedDependency(RestoreContext, dep)) {
            return false;
        }

        bool isPackageUnion = IsMultimodule && !IsDependsFinalTarget(RestoreContext.Modules, dep.To()->ElemId) && dep.To()->NodeType == EMNT_Bundle;
        if (!state.HasIncomingDep() && isPackageUnion) {
            return false;
        }

        YDIAG(Dev) << "Found DEPENDS: " << state.Top().GetFileName() << " -> " << RestoreContext.Graph.GetFileName(dep.To()) << Endl;
        if (state.HasIncomingDep() || !IsMultimodule || IsDependsFinalTarget(RestoreContext.Modules, dep.To()->ElemId)) {
            if (!IsFakeModule(RestoreContext.Modules, dep.To()->ElemId)) {
                Iterator->second.push_back(dep.To().Id());
            }
        }

        return dep.To()->NodeType == EMNT_Bundle && TBase::AcceptDep(state);
    }

private:
    bool IsModuleWithManageablePeers(TConstDepNodeRef node) const {
        if (!IsModuleType(node->NodeType)) {
            return false;
        }
        if (const TModule* mod = RestoreContext.Modules.Get(node->ElemId)) {
            return mod->GetAttrs().RequireDepManagement;
        }
        return false;
    }

private:
    TRestoreContext RestoreContext;
    TDependsToModulesClosure& Closure;
    TDependsToModulesClosure::iterator Iterator;
    bool IsMultimodule = false;
};

void TYMake::AddPackageOutputs() {
    FORCE_TRACE(U, NEvent::TStageStarted("Fill package outputs"));

    if (Conf.DependsLikeRecurse) {
        // This records extra outputs for UNIONs listed in DEPENDS
        // It is currently impossible to record these as actual module outputs,
        // so we just associate it with a StartTarget and use to fill results
        TDependsToModulesCollector collector(GetRestoreContext(), DependsToModulesClosure);
        for (const auto& t : StartTargets) {
            if (t.IsModuleTarget) {
                continue;
            }
            if (t.IsDependsTarget) {
                IterateAll(Graph, t, collector);
                collector.Reset();
            }
        }
        // Remove empty closures
        EraseNodesIf(DependsToModulesClosure, [](const auto& item) {return item.second.empty();});
    }
    FORCE_TRACE(U, NEvent::TStageFinished("Fill package outputs"));
}

void TYMake::RenderIDEProj(const TStringBuf& type, const TStringBuf& name, const TStringBuf& dir) {
    if (type.StartsWith("msvs")) {
        size_t version = FromString<size_t>(type.Tail(4));
        RenderMsvsSolution(version, name, dir);
    } else {
        YErr() << "Unsupported IDE project type " << type << Endl;
        YInfo() << "Available types: msvs<year>" << Endl;
        Diag()->HasConfigurationErrors = true;
    }
}

void TYMake::DumpMetaData() {
    if (! Conf.WriteMetaData.size())
        return;

    IOutputStream* out = &Cout;
    THolder<IOutputStream> hout;

    if (Conf.WriteMetaData != "-") {
        hout.Reset(new TOFStream(Conf.WriteMetaData));
        out = hout.Get();
    }

    NJson::TJsonValue metaJson;
    metaJson["source_root"] = Conf.SourceRoot.c_str();
    metaJson["build_root"] = Conf.BuildRoot.c_str();

    NJsonWriter::TBuf jsonWriter(NJsonWriter::HEM_UNSAFE, out);
    jsonWriter.WriteJsonValue(&metaJson, true);
}

//TODO: move to appropriate place
bool TYMake::DumpLoops() {
    FORCE_TRACE(U, NEvent::TStageStarted("Detect loops"));
    TGraphLoops Loops;
    Loops.FindLoops(Graph, StartTargets, false);
    if (Conf.ShowLoops) {
        Loops.DumpAllLoops(Graph, Conf.Cmsg());
    }

    if (Diag()->ShowAllLoops) {
        Loops.DumpAllLoops(Graph, Cerr);
    } else {
        if (Diag()->ShowDirLoops) {
            Loops.DumpDirLoops(Graph, Cerr);
        }
        if (Diag()->ShowBuildLoops) {
            Loops.DumpBuildLoops(Graph, Cerr);
        }
    }

    if (Loops.HasBadLoops()) {
        Diag()->HasConfigurationErrors = true;
        Loops.RemoveBadLoops(Graph, StartTargets);
    }

    PropagateChangeFlags(Graph, Loops, StartTargets);

    FORCE_TRACE(U, NEvent::TStageFinished("Detect loops"));
    return Loops.HasBadLoops();
}

int main_real(TBuildConfiguration& conf) {
    if (conf.DumpLicensesInfo || conf.DumpLicensesMachineInfo) {
        NSPDX::EPeerType peerType;
        if (conf.LicenseLinkType == "static") {
            peerType = NSPDX::EPeerType::Static;
        } else if (conf.LicenseLinkType == "dynamic") {
            peerType = NSPDX::EPeerType::Dynamic;
        } else {
            YErr() << "Invalid link type for license properties dump '" << conf.LicenseLinkType << "'. Only 'static' or 'dynamic' allowed." << Endl;
            return BR_FATAL_ERROR;
        }
        DoDumpLicenseInfo(conf.CommandConf, peerType, conf.DumpLicensesInfo, conf.LicenseTagVars);
        return BR_OK;
    }
    if (conf.DumpForcedDependencyManagements || conf.DumpForcedDependencyManagementsAsJson) {
        DumpFDM(conf.CommandConf, conf.DumpForcedDependencyManagementsAsJson);
        return BR_OK;
    }

    if (conf.PrintTargetAbsPath) {
        Y_ASSERT(conf.Targets.size() > 0);
        TFsPath firstBuildTarget = conf.BuildRoot / conf.Targets.front();
        Cout << firstBuildTarget << Endl;
        return BR_OK;
    }

    bool updateGraph = !conf.RebuildGraph && conf.WriteYdx.empty();
    bool useOnlyYmakeCache = conf.CachePath.size();
    TFsPath cachePath;
    if (conf.CachePath.size() > 0) {
        cachePath = TFsPath(conf.CachePath);
        conf.WriteFsCache = false;
        conf.WriteDepsCache = false;
        conf.WriteJsonCache = false;
    } else {
        cachePath = conf.YmakeCache;
    }

    auto dumpCacheFlags = [](const char* cacheName, bool rFlag, bool wFlag) {
        if (rFlag) {
            YDebug() << cacheName << " cache loading is enabled" << Endl;
        }
        if (wFlag) {
            YDebug() << cacheName << " cache saving is enabled" << Endl;
        }
    };
    dumpCacheFlags("FS", conf.ReadFsCache, conf.WriteFsCache);
    dumpCacheFlags("Deps", conf.ReadDepsCache, conf.WriteDepsCache);
    dumpCacheFlags("Json", conf.ReadJsonCache, conf.WriteJsonCache);
    dumpCacheFlags("Uids", conf.ReadUidsCache, conf.WriteUidsCache);

    bool loadGraph = updateGraph || conf.ReadFsCache || conf.ReadDepsCache;

    if (!cachePath.Exists()) {
        if (useOnlyYmakeCache) {
            YErr() << "Can not load ymake.cache. File does not exist: " << conf.YmakeCache << Endl;
            return BR_FATAL_ERROR;
        }
        loadGraph = false;
    }

    THolder<TYMake> yMake(new TYMake(conf));

    if (loadGraph) {
        if (!yMake->Load(cachePath)) {
            if (useOnlyYmakeCache) {
                YErr() << "Cache was not loaded. Stop." << Endl;
                return BR_FATAL_ERROR;
            }
            yMake.Reset(new TYMake(conf));
        }
    }
    // This should be called after Load
    yMake->PostInit();

    {
        TBuildGraphScope scope(*yMake.Get());

        TMaybe<EBuildResult> configureBuildRes;
        if (useOnlyYmakeCache) {
            configureBuildRes = StaticConfigureGraph(yMake);
        } else {
            yMake->TimeStamps.InitSession(yMake->Graph.GetFileNodeData());
            YDIAG(IPRP) << "Start of configure. CurStamp: " << int(yMake->TimeStamps.CurStamp()) << Endl;
            configureBuildRes = ConfigureGraph(yMake);
        }
        if (configureBuildRes.Defined()) {
            ConfMsgManager()->Flush();
            return configureBuildRes.GetRef();
        }

        if (!conf.WriteYdx.empty()) {
            THolder<IOutputStream> ydxOut;
            ydxOut.Reset(new TFileOutput(conf.WriteYdx));
            yMake->Yndex.WriteJSON(*ydxOut);
            ydxOut->Finish();
            return BR_OK;
        }

        if (!yMake->InitTargets()) {
            return BR_FATAL_ERROR;
        }

        yMake->AddRecursesToStartTargets();

        yMake->AddModulesToStartTargets();

        yMake->FindLostIncludes();

        yMake->ReportGraphBuildStats();
    }

    yMake->ReportModulesStats();

    yMake->SortAllEdges();

    yMake->ReportConfigureEvents();

    FORCE_TRACE(U, NEvent::TStageStarted("Save and compact"));
    if (conf.WriteFsCache || conf.WriteDepsCache) {
        yMake->Save(conf.YmakeCache, true);
    } else {
        yMake->Graph.Compact();
    }

    yMake->Modules.Compact();
    FORCE_TRACE(U, NEvent::TStageFinished("Save and compact"));

    bool hasBadLoops = yMake->DumpLoops();

    ApplyDependencyManagement(yMake->GetRestoreContext(), yMake->StartTargets, yMake->ExtraTestDeps);

    if (Diag()->HasConfigurationErrors && !yMake->Conf.KeepGoing) {
        return BR_CONFIGURE_FAILED;
    }

    if (!conf.ManagedDepTreeRoots.empty()) {
        THashSet<TNodeId> roots;
        yMake->ResolveRelationTargets(conf.ManagedDepTreeRoots, roots);
        ExplainDM(yMake->GetRestoreContext(), roots);
    }

    if (!conf.DumpDMRoots.empty()) {
        THashSet<TNodeId> roots;
        yMake->ResolveRelationTargets(conf.DumpDMRoots, roots);
        DumpDM(
            yMake->GetRestoreContext(),
            roots,
            conf.DumpDirectDM ? EManagedPeersDepth::Direct : EManagedPeersDepth::Transitive
        );
    }

    yMake->AddPackageOutputs();

    MakeUnique(yMake->StartTargets);

    CheckTransitiveRequirements(yMake->GetRestoreContext(), yMake->StartTargets);
    if (!conf.IsolatedProjects.Empty()) {
        yMake->ReportDepsToIsolatedProjects();
    }

    if (conf.DumpRecurses || conf.DumpPeers) {
        yMake->DumpDependentDirs(conf.Cmsg());
    }

    if (!hasBadLoops && Diag()->ChkPeers) {
        yMake->FindMissingPeerdirs();
    }

    if (conf.DumpDependentDirs) {
        yMake->DumpDependentDirs(conf.Cmsg(), conf.SkipDepends);
    }
    if (conf.DumpTargetDepFiles) {
        yMake->PrintTargetDeps(conf.Cmsg());
    }
    if (conf.PrintTargets) {
        yMake->DumpBuildTargets(conf.Cmsg());
    }
    if (conf.DumpSrcDeps) {
        yMake->DumpSrcDeps(conf.Cmsg());
    }

    if (conf.FullDumpGraph) {
        if (conf.DumpFiles) {
            PrintAllFiles(*yMake);
        } else if (conf.DumpDirs) {
            PrintAllDirs(*yMake);
        } else if (conf.DumpAsDot) {
            YConfErr(UserErr) << "-xGD is not implemented, use -xgD.." << Endl;
        } else {
            PrintFlatGraph(*yMake);
        }
    }

    if (conf.DumpGraphStuff) {
        yMake->DumpGraph();
    }

    if (conf.DumpExpressions) {
        YInfo() << "Expression dump:" << Endl;
        yMake->Commands.ForEachCommand([&](ECmdId id, const NPolexpr::TExpression& expr) {
            yMake->Conf.Cmsg()
                << static_cast<std::underlying_type_t<ECmdId>>(id) << " "
                << yMake->Commands.PrintCmd(expr) << "\n";
        });
        if (yMake->Conf.DumpPretty)
            yMake->Conf.Cmsg() << Endl;
    }

    if (conf.DumpModulesInfo) {
        if (conf.ModulesInfoFile.empty()) {
            DumpModulesInfo(conf.Cmsg(), yMake->GetRestoreContext(), yMake->StartTargets, conf.ModulesInfoFilter);
        } else {
            TFileOutput out{conf.ModulesInfoFile};
            DumpModulesInfo(out, yMake->GetRestoreContext(), yMake->StartTargets, conf.ModulesInfoFilter);
        }
    }

    if (conf.DumpNames) {
        yMake->Names.Dump(conf.Cmsg());
    }

    if (!conf.FindPathTo.empty()) {
        yMake->FindPathBetween(conf.FindPathFrom, conf.FindPathTo);
    }

    if (conf.WriteIDEProj) {
        yMake->RenderIDEProj(conf.WriteIDEProj, conf.IDEProjName, conf.IDEProjDir);
    }

    if (conf.WriteOwners) {
        yMake->DumpOwners();
    }

    if (Diag()->HasConfigurationErrors && !yMake->Conf.KeepGoing) {
        return BR_CONFIGURE_FAILED;
    }

    if (conf.RenderSemantics) {
        const auto* sourceRoootVar = yMake->Conf.CommandConf.Lookup("EXPORTED_BUILD_SYSTEM_SOURCE_ROOT");
        const auto* buildRoootVar = yMake->Conf.CommandConf.Lookup("EXPORTED_BUILD_SYSTEM_BUILD_ROOT");
        if (!sourceRoootVar || !buildRoootVar) {
            YConfErr(UndefVar) << "Configure variables EXPORTED_BUILD_SYSTEM_SOURCE_ROOT and EXPORTED_BUILD_SYSTEM_BUILD_ROOT are required for rendering --sem-graph" << Endl;
            return BR_CONFIGURE_FAILED;
        }
        const auto oldSourceRoot = std::exchange(yMake->Conf.SourceRoot, GetCmdValue(Get1(sourceRoootVar)));
        const auto oldBuildRoot = std::exchange(yMake->Conf.BuildRoot, GetCmdValue(Get1(buildRoootVar)));
        if (!yMake->Conf.SourceRoot.IsDefined() || !yMake->Conf.BuildRoot.IsDefined()) {
            YConfErr(Misconfiguration) << fmt::format(
                "Source root or build root for rendering --sem-graph is empty.\n\t"
                "NOTE: raw var values before converting to filesystem path are:\n\t"
                "EXPORTED_BUILD_SYSTEM_SOURCE_ROOT='{}'\n\t"
                "EXPORTED_BUILD_SYSTEM_BUILD_ROOT='{}'",
                GetCmdValue(Get1(sourceRoootVar)),
                GetCmdValue(Get1(buildRoootVar))) << Endl;
                return BR_CONFIGURE_FAILED;
        }
        RenderSemGraph(Cout, yMake->GetRestoreContext(), yMake->Commands, yMake->GetTraverseStartsContext());
        yMake->Conf.SourceRoot = oldSourceRoot;
        yMake->Conf.BuildRoot = oldBuildRoot;
        return BR_OK;
    }

    if (!conf.WriteJSON.empty()) {
        ExportJSON(*yMake);
    }

    yMake->ReportMakeCommandStats();

    if (!conf.WriteTestDart.empty()) {
        THolder<IOutputStream> dartOut;
        dartOut.Reset(new TFileOutput(conf.WriteTestDart));
        yMake->DumpTestDart(*dartOut);
        dartOut->Finish();
    }

    if (!conf.WriteJavaDart.empty()) {
        THolder<IOutputStream> dartOut;
        dartOut.Reset(new TFileOutput(conf.WriteJavaDart));
        yMake->DumpJavaDart(*dartOut);
        dartOut->Finish();
    }

    if (!conf.WriteMakeFilesDart.empty()) {
        THolder<IOutputStream> dartOut;
        dartOut.Reset(new TFileOutput(conf.WriteMakeFilesDart));
        yMake->DumpMakeFilesDart(*dartOut);
        dartOut->Finish();
    }
    yMake->Modules.ResetTransitiveInfo();

    yMake->DumpMetaData();

    if (Diag()->HasConfigurationErrors && !yMake->Conf.KeepGoing) {
        return BR_CONFIGURE_FAILED;
    }

    yMake->CommitCaches();

    return BR_OK;
}
