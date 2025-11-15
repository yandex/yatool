#include "ymake.h"

#include "json_visitor.h"
#include "mkcmd.h"
#include "blacklist_checker.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/compute_reachability.h>
#include <devtools/ymake/propagate_change_flags.h>
#include <devtools/ymake/python_runtime.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/use_awaitable.hpp>

#include <util/generic/ptr.h>

namespace {
    enum class ESortPriority {
        Peers           = 1,
        DirectPeers     = 2,
        OutTogetherBack = 3,
        Default         = 4,
        IncludeFile     = 5,
        DirectTools     = 6
    };

    inline ESortPriority ComputeEdgePriority(EMakeNodeType fromType, EDepType depType, EMakeNodeType toType) {
        if (IsDirectPeerdirDep(fromType, depType, toType)) {
            return ESortPriority::DirectPeers;
        }
        if (IsPeerdirDep(fromType, depType, toType)) {
            return ESortPriority::Peers;
        }
        if (IsDirectToolDep(fromType, depType, toType)) {
            return ESortPriority::DirectTools;
        }
        if (IsIncludeFileDep(fromType, depType, toType) || IsPropertyFileDep(depType, toType)) {
            return ESortPriority::IncludeFile;
        }
        if (depType == EDT_OutTogetherBack) {
            return ESortPriority::OutTogetherBack;
        }
        return ESortPriority::Default;
    }
}

TNodeEdgesComparator::TNodeEdgesComparator(TConstDepNodeRef node):
    FromType{node->NodeType},
    Graph{TDepGraph::Graph(node)}
{}

bool TNodeEdgesComparator::operator() (const TDepGraph::TEdge& edge1, const TDepGraph::TEdge& edge2) const noexcept {
    auto node1 = edge1.IsValid() ? Graph[edge1.Id()] : Graph.GetInvalidNode();
    auto node2 = edge2.IsValid() ? Graph[edge2.Id()] : Graph.GetInvalidNode();
    if (!node1.IsValid() && node2.IsValid()) {
        return true;
    }
    if (!node1.IsValid() || !node2.IsValid()) {
        return false;
    }

    const ESortPriority priority1 = ComputeEdgePriority(FromType, edge1.Value(), node1->NodeType);
    const ESortPriority priority2 = ComputeEdgePriority(FromType, edge2.Value(), node2->NodeType);
    if (priority1 == ESortPriority::IncludeFile && priority2 == ESortPriority::IncludeFile) {
        return Graph.GetFileName(node1) < Graph.GetFileName(node2);
    }
    return static_cast<ui32>(priority1) < static_cast<ui32>(priority2);
}

void TYMake::SortAllEdges() {
    NYMake::TTraceStage stageTracer{"Sort edges"};
    for (auto node: Graph.Nodes()) {
        if (node.IsValid()) {
            node.SortEdges(TNodeEdgesComparator{node});
        }
    }
}

void TYMake::CheckBlacklist() {
    NYMake::TTraceStage stageTracer{"Check blacklist"};
    TRestoreContext restoreContext(Conf, Graph, Modules);
    TRestoreContext recurseRestoreContext(Conf, RecurseGraph, Modules);
    TBlacklistChecker blacklistChecker(restoreContext, StartTargets, recurseRestoreContext, RecurseStartTargets);
    blacklistChecker.CheckAll();
}

void TYMake::CheckIsolatedProjects() {
    NYMake::TTraceStage stageTracer{"Check isolated projects"};
    TRestoreContext restoreContext(Conf, Graph, Modules);
    TRestoreContext recurseRestoreContext(Conf, RecurseGraph, Modules);
    Conf.IsolatedProjects.CheckAll(restoreContext, StartTargets, recurseRestoreContext, RecurseStartTargets);
}

void TYMake::TransferStartDirs() {
    for (const auto& dir : Conf.StartDirs) {
        TString curDir = NPath::ConstructPath(NPath::FromLocal(dir), NPath::Source);
        CurStartDirs_.push_back(Names.AddName(EMNT_Directory, curDir));
    }
}

void TYMake::PostInit() {
    IncParserManager.InitManager(Conf.ParserPlugins);
    LoadPatch();
    Names.FileConf.InitAfterCacheLoading();
    FSCacheMonEvent();
    DepsCacheMonEvent();
}

void TYMake::CheckStartDirsChanges() {
    TransferStartDirs();
    // Compare order-independently since the order may vary for tool runs
    if (TSet<ui32>{PrevStartDirs_.begin(), PrevStartDirs_.end()} != TSet<ui32>{CurStartDirs_.begin(), CurStartDirs_.end()}) {
        HasGraphStructuralChanges_ = true;
        YDebug() << "Graph has structural changes because start dirs are different" << Endl;
    }
}

static void SafeRemove(const TFsPath& path) {
    try {
        if (path.IsDefined())
            path.DeleteIfExists();
    } catch(...) {
        // Ignore any errors
    }
}

TYMake::~TYMake() {
    SafeRemove(DepCacheTempFile);
    SafeRemove(DMCacheTempFile);
    SafeRemove(UidsCacheTempFile);
}

TModuleResolveContext TYMake::GetModuleResolveContext(const TModule& mod) {
    Y_ASSERT(UpdIter != nullptr);
    return MakeModuleResolveContext(mod, Conf, Graph, *UpdIter, IncParserManager.Cache);
}

TRestoreContext TYMake::GetRestoreContext() {
    return {Conf, Graph, Modules};
}

TTraverseStartsContext TYMake::GetTraverseStartsContext() const noexcept {
    return {StartTargets, RecurseStartTargets, ModuleStartTargets};
}

TFileProcessContext TYMake::GetFileProcessContext(TModule* module, TAddDepAdaptor& node) {
    Y_ASSERT(module);
    Y_ASSERT(UpdIter);
    return {Conf, GetModuleResolveContext(*module), UpdIter->State, *module, node};
}

void TYMake::ReportGraphBuildStats() {
    Names.FileConf.ReportStats();
    IncParserManager.Stats.Report();
    Parser->ReportStats();
    Graph.ReportStats();
    UpdIter->Stats.Report();
    UpdIter->ResolveStats.Report();
}

void TYMake::ReportModulesStats() {
    Modules.ReportStats();
}

void TYMake::ReportMakeCommandStats() {
    TMakeCommand::ReportStats();
}

asio::awaitable<void> TYMake::AddStartTarget(TConfigurationExecutor exec, const TString& dir, const TString& tag, bool followRecurses) {
    return asio::co_spawn(exec, [dir, tag, followRecurses, this] () -> asio::awaitable<void> {
        NYMake::TPythonThreadStateScope st{(PyInterpreterState*)Conf.SubState};
        TString dirPath = NPath::ConstructPath(NPath::FromLocal(TStringBuf{dir}), NPath::Source);
        auto elemId = Names.AddName(EMNT_Directory, dirPath);
        TNodeId nodeId = TNodeId::Invalid;
        if (followRecurses) {
            nodeId = UpdIter->RecursiveAddStartTarget(EMNT_Directory, elemId, &Modules.GetRootModule());
        } else {
            nodeId = UpdIter->RecursiveAddNode(EMNT_Directory, elemId, &Modules.GetRootModule());
        }
        if (nodeId != TNodeId::Invalid) {
            StartTargets.push_back({nodeId, 0, tag});
        }
        co_return;
    }, asio::use_awaitable);
}

asio::awaitable<void> TYMake::AddTarget(TConfigurationExecutor exec, const TString& dir) {
    return asio::co_spawn(exec, [dir, this] () -> asio::awaitable<void> {
        NYMake::TPythonThreadStateScope st{(PyInterpreterState*)Conf.SubState};
        TString dirPath = NPath::ConstructPath(NPath::FromLocal(TStringBuf{dir}), NPath::Source);
        auto elemId = Names.AddName(EMNT_Directory, dirPath);
        UpdIter->RecursiveAddNode(EMNT_Directory, elemId, &Modules.GetRootModule());
        co_return;
    }, asio::use_awaitable);
}

void TYMake::ComputeReachableNodes() {
    if (DepsCacheLoaded_) {
        NYMake::TTraceStage scopeTracer{"Reset reachable nodes"};
        NComputeReachability::ResetReachableNodes(Graph);
    }

    NYMake::TTraceStage scopeTracer{"Set reachable nodes"};
    NComputeReachability::ComputeReachableNodes(Graph, StartTargets);
}

void TYMake::UpdateExternalFilesChanges() {
    NYMake::TTraceStage scopeTracer{"Update External changes for Grand Bypass"};

    SetLocalChangesForGrandBypass(Graph, StartTargets);

    auto& fileConf = Names.FileConf;
    auto externalChanges = fileConf.GetExternalChanges();
    for (auto id : externalChanges) {
        for (ui8 i = 0; i < static_cast<ui8>(ELinkType::ELT_COUNT); ++i) {
            auto linkType = static_cast<ELinkType>(i);
            auto elemId = TFileId::CreateElemId(linkType, id);
            auto node = Graph.GetFileNodeById(elemId);
            if (node.IsValid()) {
                if (node->State.GetReachable() && node->NodeType == EMNT_File) {
                    fileConf.GetFileById(elemId)->GetContent();
                    node->State.SetLocalChanges(false, true);
                }
            }
        }
    }
}

void TYMake::UpdateUnreachableExternalFileChanges() {
    NYMake::TTraceStage scopeTracer{"Update Unreachable file from External changes"};

    auto& fileConf = Names.FileConf;
    auto externalChanges = fileConf.GetExternalChanges();
    for (auto id : externalChanges) {
        for (ui8 i = 0; i < static_cast<ui8>(ELinkType::ELT_COUNT); ++i) {
            auto linkType = static_cast<ELinkType>(i);
            auto elemId = TFileId::CreateElemId(linkType, id);
            auto node = Graph.GetFileNodeById(elemId);
            if (node.IsValid()) {
                if (!node->State.GetReachable() && node->NodeType == EMNT_File) {
                    auto& fileData = fileConf.GetFileDataById(elemId);
                    fileData.HashSum = {};
                    fileData.Size = 0;
                    fileData.LastCheckedStamp = TTimeStamps::Never;
                    fileData.RealModStamp = 0;
                }
            }
        }
    }
}

void TYMake::LoadJsonCacheAsync(asio::any_io_executor exec) {
    JSONCacheLoadingCompletedPtr = MakeAtomicShared<asio::experimental::concurrent_channel<void(asio::error_code, THolder<TMakePlanCache>)>>(exec, 1u);
    asio::co_spawn(exec, [this]() -> asio::awaitable<void> {
        try {
            auto JSONCache = MakeHolder<TMakePlanCache>(Conf);
            JSONCacheLoaded(JSONCache->LoadFromFile());
            co_await JSONCacheLoadingCompletedPtr->async_send(std::error_code{}, std::move(JSONCache));
        } catch (const std::exception& e) {
            YDebug() << "JSON cache failed to be loaded: " << e.what() << Endl;
            JSONCacheLoadingCompletedPtr->try_send(std::error_code{}, nullptr);
        }
    }, asio::detached);
}

void TYMake::LoadUidsAsync(asio::any_io_executor exec) {
    UidsCacheLoadingCompletedPtr = MakeAtomicShared<asio::experimental::concurrent_channel<void(asio::error_code, THolder<TUidsData>)>>(exec, 1u);
    asio::co_spawn(exec, [this]() -> asio::awaitable<void> {
        try {
            auto UidsCache = MakeHolder<TUidsData>(GetRestoreContext(), StartTargets);
            LoadUids(UidsCache.Get());
            co_await UidsCacheLoadingCompletedPtr->async_send(std::error_code{}, std::move(UidsCache));
        } catch (const std::exception& e) {
            YDebug() << "Uids cache failed to be loaded: " << e.what() << Endl;
            UidsCacheLoadingCompletedPtr->try_send(std::error_code{}, nullptr);
        }
    }, asio::detached);
}
