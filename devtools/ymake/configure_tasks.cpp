#include "configure_tasks.h"
#include "evlog_server.h"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>

namespace {
    static asio::awaitable<TMaybe<EBuildResult>> ConfigureGraph(THolder<TYMake>& yMake, TConfigurationExecutor exec) {
        NYMake::TTraceStageWithTimer configureTimer("Configure graph", MON_NAME(EYmakeStats::ConfigureGraphTime));
        try {
            co_await yMake->BuildDepGraph(exec);
        } catch (const TNotImplemented& e) {
            TRACE(G, NEvent::TRebuildGraph(e.what()));
            YConfWarn(KnownBug) << "Graph needs to be rebuilt: " << e.what() << Endl;
            co_return TMaybe<EBuildResult>(BR_RETRYABLE_ERROR);
        } catch (const TInvalidGraph& e) {
            TRACE(G, NEvent::TRebuildGraph(e.what()));
            YConfWarn(KnownBug) << "Graph update failed: " << e.what() << Endl;
            yMake->UpdIter->DumpStackW();
            co_return TMaybe<EBuildResult>(BR_RETRYABLE_ERROR);
        } catch(const yexception& error) {
            YErr() << error.what() << Endl;
            co_return TMaybe<EBuildResult>(BR_FATAL_ERROR);
        }
        co_return TMaybe<EBuildResult>();
    }
}

asio::awaitable<void> TYMake::BuildDepGraph(TConfigurationExecutor exec) {
    UpdIter->RestorePropsToUse();

    TModule& rootModule = Modules.GetRootModule();
    if (!Conf.ReadStartTargetsFromEvlog) {  // in server mode start targets are already added to the graph by evlog server
        for (const auto& dir : Conf.StartDirs) {
            co_await AddStartTarget(exec, dir);
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
                    co_await asio::co_spawn(exec, [&]() -> asio::awaitable<void> {
                        UpdIter->RecursiveAddStartTarget(to.NodeType, to.ElemId, &rootModule);
                        co_return;
                    }, asio::use_awaitable);
                }
            }
        }
    }

    bool prevTraverseAllRecurses = Conf.ShouldTraverseAllRecurses();
    bool prevTraverseRecurses = Conf.ShouldTraverseRecurses();
    Conf.SetTraverseAllRecurses(false);
    Conf.SetTraverseRecurses(false);

    for (size_t idx = 0; idx < UpdIter->DependsQueue.GetAllNodes().size(); ++idx) {
        auto node = UpdIter->DependsQueue.GetAllNodes()[idx];
        const auto* deps = UpdIter->DependsQueue.GetDeps(node);
        if (!deps) {
            continue;
        }
        for (const auto& [to, dep] : *deps) {
            if (IsDependsDep(node.NodeType, dep, to.NodeType)) {
                co_await asio::co_spawn(exec, [&]() -> asio::awaitable<void> {
                    UpdIter->RecursiveAddStartTarget(to.NodeType, to.ElemId, &rootModule);
                    co_return;
                }, asio::use_awaitable);
            }
        }
    }

    for (auto dependsNode : UpdIter->DependsQueue.GetAllNodes()) {
        auto deps = UpdIter->DependsQueue.GetDeps(dependsNode);
        if (deps) {
            for (const auto& [to, dep] : *deps) {
                UpdIter->RecurseQueue.AddEdge(dependsNode, to, dep);
            }
        }
    }

    Conf.SetTraverseAllRecurses(prevTraverseAllRecurses);
    Conf.SetTraverseRecurses(prevTraverseRecurses);

    UpdIter->DelayedSearchDirDeps.Flush(*Parser, Graph);
    Graph.RelocateNodes(Parser->RelocatedNodes);
    co_return;
}

asio::awaitable<TMaybe<EBuildResult>> RunConfigureAsync(THolder<TYMake>& yMake, TConfigurationExecutor exec) {
    YDebug() << "Pseudo parallel configure START" << Endl;
    yMake->TimeStamps.InitSession(yMake->Graph.GetFileNodeData());
    auto buildRes = co_await ConfigureGraph(yMake, exec);
    YDebug() << "Pseudo parallel configure DONE" << Endl;
    co_return buildRes;
}

asio::awaitable<void> ProcessEvlogAsync(THolder<TYMake>& yMake, TBuildConfiguration& conf, NForeignTargetPipeline::TLineReader& input, TConfigurationExecutor exec) {
    YDebug() << "Pseudo parallel server mode START" << Endl;
    yMake->TimeStamps.InitSession(yMake->Graph.GetFileNodeData());
    NEvlogServer::TServer evlogServer{exec, *yMake, conf};
    // For now it's a synchronous reader w/o any buffering.
    // The client must ensure they use non-blocking writes on their side,
    // like it's done for tool evlog in devtools/ya/build/graph.py:_ToolTargetsQueue
    co_await evlogServer.ProcessStreamBlocking(input);
    YDebug() << "Pseudo parallel server mode DONE" << Endl;
    co_return;
}
