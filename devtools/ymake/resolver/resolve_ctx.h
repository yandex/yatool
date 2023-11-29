#pragma once

#include <devtools/ymake/options/roots_options.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>

struct TRootsOptions;
class TDepGraph;
class TUpdIter;
class TParsersCache;

namespace NGraphUpdater {
    enum class ENodeStatus {
        Unknown,
        Waiting,
        Processing,
        Ready
    };

    const ENodeStatus DefaultStatus = ENodeStatus::Ready;

    using TNodeStatusChecker = std::function<ENodeStatus(const TDepTreeNode&)>;
}

struct TResolveContext {
    const TRootsOptions& Conf;
    TDepGraph& Graph;
    const TOwnEntries& OwnEntries;
    const TParsersCache& ParsersCache;

    NGraphUpdater::TNodeStatusChecker NodeStatusChecker;

    TResolveContext(const TRootsOptions& conf, TDepGraph& graph, const TOwnEntries& ownEntries,
                    const TParsersCache& parsersCache, NGraphUpdater::TNodeStatusChecker nodeStatusChecker = {})
        : Conf(conf)
        , Graph(graph)
        , OwnEntries(ownEntries)
        , ParsersCache(parsersCache)
        , NodeStatusChecker(nodeStatusChecker)
    {
    }

    TResolveContext(const TResolveContext&) = default;

    NGraphUpdater::ENodeStatus CheckNodeStatus(const TDepTreeNode& node) {
        if (!NodeStatusChecker) {
            return NGraphUpdater::DefaultStatus;
        }
        return NodeStatusChecker(node);
    }
};
