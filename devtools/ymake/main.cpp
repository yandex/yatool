#include "build_result.h"
#include "builtin_macro_consts.h"
#include "conf.h"
#include "dump_graph_info.h"
#include "dependency_management.h"
#include "diag_reporter.h"
#include "export_json.h"
#include "managed_deps_iter.h"
#include "node_printer.h"
#include "prop_names.h"
#include "propagate_change_flags.h"
#include "recurse_graph.h"
#include "sem_graph.h"
#include "transitive_requirements_check.h"
#include "ymake.h"
#include "configure_tasks.h"

#include <devtools/ymake/build_graph_scope.h>
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
#include <devtools/ymake/evlog_server.h>
#include <devtools/ymake/context_executor.h>

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

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/detached.hpp>

#include <fmt/format.h>

#include <Python.h>

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
    , Yndex(Conf.CommandDefinitions, Conf.CommandReferences)
    , Modules(Names, conf.PeersRules, Conf)
{
    TimeStamps.StartSession();
    Diag()->Where.clear();
}

static TMaybe<EBuildResult> StaticConfigureGraph(THolder<TYMake>& yMake) {
    NYMake::TTraceStageWithTimer configureTimer("Configure readonly graph", MON_NAME(EYmakeStats::ConfigureReadonlyGraphTime));
    TBuildConfiguration& conf = yMake->Conf;
    for (size_t i = 0; i < conf.StartDirs.size(); ++i) {
        TString curDir = NPath::ConstructPath(NPath::FromLocal(conf.StartDirs[i]), NPath::Source);
        ui64 node = yMake->Graph.Names().FileConf.GetIdNx(curDir);
        if (!node)
            continue;
        const TNodeId offs = yMake->Graph.GetFileNodeById(node).Id();
        if (offs != TNodeId::Invalid)
            yMake->StartTargets.push_back(offs);
    }
    return yMake->StartTargets.size() ? TMaybe<EBuildResult>() : TMaybe<EBuildResult>(BR_CONFIGURE_FAILED);
}

TNodeId TYMake::GetUserTarget(const TStringBuf& target) const {
    YDebug() << "Searching target '" << target << "'" << Endl;
    TString from = NPath::ConstructPath(target, NPath::Source);
    TNodeId nodeId = Graph.GetFileNode(from).Id();
    if (nodeId == TNodeId::Invalid) {
        from = NPath::ConstructPath(target, NPath::Build);
        nodeId = Graph.GetFileNode(from).Id();
    }
    if (nodeId == TNodeId::Invalid) {
        from = target;
        nodeId = Graph.GetFileNode(from).Id();
    }
    YDebug() << "Selected target '" << from << "' with id " << nodeId << Endl;
    return nodeId;
}

bool TYMake::InitTargets() {
    for (const TFsPath& target: Conf.Targets) {
        TNodeId offs = GetUserTarget(NPath::FromLocal(target));
        if (offs == TNodeId::Invalid) {
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
            TStringBuf macro = depType == EDT_Include ? NMacro::RECURSE : NProps::DEPENDS;
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
    // When in servermode, we shouldn't filter root nodes by language since
    // they aren't "start targets" in full sense but just transitions within a multiplatform graph.
    bool shouldCheckLanguages = !Conf.ReadStartTargetsFromEvlog;
    if (shouldCheckLanguages) {
        const char* filterLangs = "BUILD_LANGUAGES";
        auto langCmd = Conf.CommandConf.Get1(filterLangs);
        if (langCmd) {
            auto languagesStr = GetCmdValue(langCmd);
            Split(languagesStr, " ", buildLanguages);
        }
        shouldCheckLanguages = !buildLanguages.empty();
    }
    auto langFilter = [&buildLanguages, shouldCheckLanguages](const TModule& mod) {
        if (!shouldCheckLanguages) {
            return true;
        }
        auto langIt = std::find(buildLanguages.begin(), buildLanguages.end(), mod.GetLang());
        return langIt != buildLanguages.end();
    };
    auto tagFilter = [](const TModule& mod, const TTarget& target) {
        return target.Tag.empty() || target.Tag == mod.GetTag();
    };
    auto startTargetsModules = GetStartTargetsModules(Graph, StartTargets);
    for (const auto& target : startTargetsModules) {
        auto mod = Modules.Get(Graph.GetFileName(Graph.Get(target.Id)).GetElemId());
        Y_ASSERT(mod);
        if (target.IsDependsTarget || mod->IsStartTarget() && langFilter(*mod) && tagFilter(*mod, target)) {
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
            if (dep.To()->NodeType == EMNT_Property && GetPropertyName(TDepGraph::GetCmdName(dep.To()).GetStr()) == NProps::MULTIMODULE) {
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

void TYMake::ComputeDependsToModulesClosure() {
    if (DependsToModulesClosureCollected) {
        return;
    }
    // This records extra outputs for UNIONs listed in DEPENDS
    // It is currently impossible to record these as actual module outputs,
    // so we just associate it with a StartTarget and use to fill results
    NYMake::TTraceStage scopeTracer{"Compute DEPENDS to modules closure"};
    TDependsToModulesCollector collector(GetRestoreContext(), DependsToModulesClosure);
    for (const auto& t : StartTargets) {
        if (t.IsModuleTarget || !t.IsDependsTarget) {
            continue;
        }
        IterateAll(Graph, t, collector);
        collector.Reset();
    }
    // Remove empty closures
    EraseNodesIf(DependsToModulesClosure, [](const auto& item) {return item.second.empty();});
    DependsToModulesClosureCollected = true;
}

void TYMake::GetDependsToModulesClosure() {
    if (Conf.DependsLikeRecurse) {
        ComputeDependsToModulesClosure();
    }
}

void TYMake::DumpMetaData() {
    if (! Conf.WriteMetaData.size())
        return;

    IOutputStream* out = Conf.OutputStream.Get();
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
    const auto Loops = TGraphLoops::Find(Graph, StartTargets, false);
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
        ConfMsgManager()->HasConfigurationErrors = true;
        Loops.RemoveBadLoops(Graph, StartTargets);
    }

    PropagateChangeFlags(Graph, Loops, StartTargets);

    FORCE_TRACE(U, NEvent::TStageFinished("Detect loops"));
    return Loops.HasBadLoops();
}

void TraceBypassEvent(const TBuildConfiguration& conf, const TYMake& yMake) {
    NEvent::TBypassConfigure bypassEvent;
    bypassEvent.SetMaybeEnabled(conf.ShouldUseGrandBypass());
    bypassEvent.SetEnabled(yMake.CanBypassConfigure());
    conf.ForeignTargetWriter->WriteBypassLine(NYMake::EventToStr(bypassEvent));
};

asio::awaitable<TMaybe<EBuildResult>> ConfigureStage(THolder<TYMake>& yMake, TBuildConfiguration& conf, TConfigurationExecutor exec) {
    TBuildGraphScope scope(*yMake.Get());

    if (conf.ReadStartTargetsFromEvlog) {
        co_await ProcessEvlogAsync(yMake, conf, *conf.ForeignTargetReader, exec);

        if (conf.StartDirs.empty()) {
            conf.ForeignTargetWriter->WriteLine(NYMake::EventToStr(NEvent::TAllForeignPlatformsReported{}));
            FORCE_TRACE(U,  NEvent::TCacheIsExpectedToBeEmpty{conf.YmakeCache.GetPath()});
            co_return BR_OK;
        }
    }

    yMake->CheckStartDirsChanges();
    yMake->GraphChangesPredictionEvent();
    TraceBypassEvent(conf, *yMake);

    if (yMake->CanBypassConfigure()) {
        conf.DoNotWriteAllCaches();
    } else {
        ConfMsgManager()->ClearTopLevelMessages();
    }

    TMaybe<EBuildResult> configureBuildRes;
    if (conf.ShouldUseOnlyYmakeCache() || yMake->CanBypassConfigure()) {
        configureBuildRes = StaticConfigureGraph(yMake);
    } else {
        configureBuildRes = co_await RunConfigureAsync(yMake, exec);
    }

    if (configureBuildRes.Defined()) {
        ConfMsgManager()->Flush();
        co_return configureBuildRes.GetRef();
    }

    if (!conf.WriteYdx.empty()) {
        THolder<IOutputStream> ydxOut(new TFileOutput(conf.WriteYdx));
        yMake->Yndex.WriteJSON(*ydxOut);
        ydxOut->Finish();
        co_return BR_OK;
    }

    if (!yMake->InitTargets()) {
        co_return BR_FATAL_ERROR;
    }

    if (yMake->CanBypassConfigure()) {
        yMake->SetStartTargetsFromCache();
        yMake->UpdateExternalFilesChanges();
    } else {
        yMake->AddRecursesToStartTargets();
        yMake->AddModulesToStartTargets();
        yMake->FindLostIncludes();
    }

    yMake->ReportGraphBuildStats();
    co_return TMaybe<EBuildResult>();
}

TMaybe<EBuildResult> EarlyDumpStage(TBuildConfiguration& conf) {
    if (conf.DumpLicensesInfo || conf.DumpLicensesMachineInfo) {
        return TMaybe<EBuildResult>(DumpLicenseInfo(conf));
    }
    if (conf.DumpForcedDependencyManagements || conf.DumpForcedDependencyManagementsAsJson) {
        DumpFDM(conf.CommandConf, conf.DumpForcedDependencyManagementsAsJson);
        return TMaybe<EBuildResult>(BR_OK);
    }

    if (conf.PrintTargetAbsPath) {
        return TMaybe<EBuildResult>(PrintAbsTargetPath(conf));
    }
    return TMaybe<EBuildResult>();
}

TMaybe<EBuildResult> PrepareStage(THolder<TYMake>& yMake, TBuildConfiguration& conf) {
    if (!conf.CachePath.Exists() && conf.ShouldUseOnlyYmakeCache()) {
        YErr() << "Can not load ymake.cache. File does not exist: " << conf.YmakeCache << Endl;
        return BR_FATAL_ERROR;
    }

    if (conf.ShouldLoadGraph()) {
        if (!yMake->Load(conf.CachePath)) {
            if (conf.ShouldUseOnlyYmakeCache()) {
                YErr() << "Cache was not loaded. Stop." << Endl;
                return BR_FATAL_ERROR;
            }
            yMake.Reset(new TYMake(conf));
        }
    }

    // This should be called after Load
    yMake->PostInit();
    // This should be called after cache loading because all errors including top level errors restore from cache
    ConfMsgManager()->EnableDelay();

    return TMaybe<EBuildResult>();
}

bool PostConfigureStage(TBuildConfiguration& conf, THolder<TYMake>& yMake) {
    yMake->ReportModulesStats();

    if (!yMake->CanBypassConfigure()) {
        yMake->ComputeReachableNodes();
        yMake->SortAllEdges();
    }

    yMake->ReportForeignPlatformEvents();

    if (!yMake->CanBypassConfigure()) {
        yMake->Compact();
    }

    bool hasBadLoops = yMake->DumpLoops();
    if (hasBadLoops) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::BadLoops), true);
        if (!conf.IsYmakeSaveAllCachesWhenBadLoops()) {
            conf.DoNotWriteAllCaches();
        }
    }
    return hasBadLoops;
}

asio::awaitable<TMaybe<EBuildResult>> AnalysesStage(TBuildConfiguration& conf, THolder<TYMake>& yMake, bool hasBadLoops) {
    if (!yMake->CanBypassConfigure() || conf.IsBlacklistHashChanged()) {
        yMake->CheckBlacklist();
    }
    if (!yMake->CanBypassConfigure() || conf.IsIsolatedProjectsHashChanged()) {
        yMake->CheckIsolatedProjects();
    }

    // Load Dependency Management and DependsToModulesClosure from cache if possible,
    // otherwise compute them and save to cache.
    if (auto result = yMake->ApplyDependencyManagement()) {
        co_return result.GetRef();
    }

    MakeUnique(yMake->StartTargets);

    if (!yMake->CanBypassConfigure() && conf.CheckTransitiveRequirements) {
        CheckTransitiveRequirements(yMake->GetRestoreContext(), yMake->StartTargets);
    }

    if (!hasBadLoops && Diag()->ChkPeers && !yMake->CanBypassConfigure()) {
        yMake->FindMissingPeerdirs();
    }
    co_return TMaybe<EBuildResult>();
}

asio::awaitable<void> ReportConfigureErrors(THolder<TYMake>& yMake) {
    // after reporting configuration errors from the cache, all other errors must be reported immediately,
    // so we disable the delay here
    ConfMsgManager()->DisableDelay();
    yMake->ReportConfigureEvents();
    co_return;
}

asio::awaitable<void> SaveCaches(TBuildConfiguration& conf, THolder<TYMake>& yMake) {
    if (conf.WriteFsCache || conf.WriteDepsCache) {
        yMake->UpdateUnreachableExternalFileChanges();
        yMake->Save(conf.YmakeCache, true);
    }
    co_return;
}

asio::awaitable<TMaybe<EBuildResult>> RenderGraph(TBuildConfiguration& conf, THolder<TYMake>& yMake, asio::any_io_executor exec) {
    if (conf.RenderSemantics) {
        const auto* sourceRootVar = yMake->Conf.CommandConf.Lookup(NVariableDefs::VAR_EXPORTED_BUILD_SYSTEM_SOURCE_ROOT);
        const auto* buildRootVar = yMake->Conf.CommandConf.Lookup(NVariableDefs::VAR_EXPORTED_BUILD_SYSTEM_BUILD_ROOT);
        if (!sourceRootVar || !buildRootVar) {
            YConfErr(UndefVar) << "Configure variables EXPORTED_BUILD_SYSTEM_SOURCE_ROOT and EXPORTED_BUILD_SYSTEM_BUILD_ROOT are required for rendering --sem-graph" << Endl;
            co_return BR_CONFIGURE_FAILED;
        }
        const auto oldSourceRoot = std::exchange(yMake->Conf.SourceRoot, GetCmdValue(Get1(sourceRootVar)));
        const auto oldBuildRoot = std::exchange(yMake->Conf.BuildRoot, GetCmdValue(Get1(buildRootVar)));
        if (!yMake->Conf.SourceRoot.IsDefined() || !yMake->Conf.BuildRoot.IsDefined()) {
            YConfErr(Misconfiguration) << fmt::format(
                "Source root or build root for rendering --sem-graph is empty.\n\t"
                "NOTE: raw var values before converting to filesystem path are:\n\t"
                "EXPORTED_BUILD_SYSTEM_SOURCE_ROOT='{}'\n\t"
                "EXPORTED_BUILD_SYSTEM_BUILD_ROOT='{}'",
                GetCmdValue(Get1(sourceRootVar)),
                GetCmdValue(Get1(buildRootVar))) << Endl;
                co_return BR_CONFIGURE_FAILED;
        }
        RenderSemGraph(*yMake->Conf.OutputStream.Get(), yMake->GetRestoreContext(), yMake->Commands, yMake->GetTraverseStartsContext());
        yMake->Conf.SourceRoot = oldSourceRoot;
        yMake->Conf.BuildRoot = oldBuildRoot;
        co_return BR_OK;
    }

    if (!conf.WriteJSON.empty()) {
        co_await ExportJSON(*yMake, exec);
    }

    co_return TMaybe<EBuildResult>();
}

void DumpDarts(TBuildConfiguration& conf, THolder<TYMake>& yMake) {
    yMake->ReportMakeCommandStats();

    yMake->Conf.SourceRoot = "$(SOURCE_ROOT)";
    yMake->Conf.BuildRoot = "$(BUILD_ROOT)";

    TDartManager dartManager(*yMake);

    if (!conf.WriteTestDart.empty()) {
        dartManager.Dump(TDartManager::EDartType::Test, conf.WriteTestDart);
    }

    if (!conf.WriteJavaDart.empty()) {
        dartManager.Dump(TDartManager::EDartType::Java, conf.WriteJavaDart);
    }

    if (!conf.WriteMakeFilesDart.empty()) {
        dartManager.Dump(TDartManager::EDartType::Makefiles, conf.WriteMakeFilesDart);
    }

    yMake->Modules.ResetTransitiveInfo();

    yMake->DumpMetaData();
}

asio::awaitable<int> main_real(TBuildConfiguration& conf, TExecutorWithContext<TExecContext> exec) {
    THolder<TYMake> yMake(new TYMake(conf));
    auto serial_exec = asio::make_strand(exec);

    TMaybe<EBuildResult> result = EarlyDumpStage(conf);
    if (result.Defined()) {
        co_return result.GetRef();
    }

    result = PrepareStage(yMake, conf);
    if (result.Defined()) {
        co_return result.GetRef();
    }

    result = co_await asio::co_spawn(exec, ConfigureStage(yMake, conf, serial_exec), asio::use_awaitable);
    if (result.Defined()) {
        co_return result.GetRef();
    }

    bool hasBadLoops = PostConfigureStage(conf, yMake);

    result = co_await asio::co_spawn(exec, AnalysesStage(conf, yMake, hasBadLoops), asio::use_awaitable);
    if (result.Defined()) {
        co_return result.GetRef();
    }

    co_await asio::co_spawn(exec, [&conf, &yMake]() -> asio::awaitable<void> {
        PerformDumps(conf, *yMake);
        co_return;
    }, asio::use_awaitable);

    co_await asio::co_spawn(exec, ReportConfigureErrors(yMake), asio::use_awaitable);
    co_await asio::co_spawn(exec, SaveCaches(conf, yMake), asio::use_awaitable);

    if (ConfMsgManager()->HasConfigurationErrors && !yMake->Conf.KeepGoing) {
        co_return BR_CONFIGURE_FAILED;
    }

    result = co_await asio::co_spawn(exec, RenderGraph(conf, yMake, exec), asio::use_awaitable);
    if (result.Defined()) {
        auto r = result.GetRef();
        if (BR_OK == r) {
            yMake->CommitCaches();
        }
        co_return r;
    }

    DumpDarts(conf, yMake);
    if (ConfMsgManager()->HasConfigurationErrors && !yMake->Conf.KeepGoing) {
        co_return BR_CONFIGURE_FAILED;
    }

    yMake->CommitCaches();

    co_return BR_OK;
}
