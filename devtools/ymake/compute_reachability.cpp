#include "compute_reachability.h"

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/query.h>

namespace NComputeReachability {
    struct TStackData {
        bool IsIndirectPeerdir = false;
    };
    using TStateItem = TGraphIteratorStateItem<TStackData, false>;

    class TVisitor: public TFilteredNoReentryStatsVisitor<TEntryStats, TStateItem> {
    public:
        using TBase = TFilteredNoReentryStatsVisitor<TEntryStats, TStateItem>;

        TVisitor()
            : TBase{TDependencyFilter{TDependencyFilter::SkipRecurses | TDependencyFilter::SkipAddincls}}
        {
        }

        bool AcceptDep(TState& state) {
            if (state.Top().IsIndirectPeerdir) {
                const auto& dep = state.NextDep();
                if (EDT_Include == *dep && IsMakeFileType(dep.To()->NodeType)) {
                    // Do nothing
                } else {
                     return false;
                }
            }
            return TBase::AcceptDep(state);
        }

        bool Enter(TState& state) {
            bool fresh  = TBase::Enter(state);

            if (fresh) {
                if (state.HasIncomingDep() && IsPeerdirDep(state.IncomingDep())) {
                    state.Top().IsIndirectPeerdir = true;
                }
                state.TopNode()->State.SetReachable(true);
            }

            return fresh;
        }
    };

    void ResetReachableNodes(TDepGraph& graph) {
        for (auto node : graph.Nodes()) {
            node->State.SetReachable(false);
        }
    }

    void ComputeReachableNodes(TDepGraph& graph, const TFileConf& /* fileConf */, TVector<TTarget>& startTargets) {
        TVisitor visitor;
        IterateAll(graph, startTargets, visitor, [](const TTarget& t) -> bool { return t.IsModuleTarget; });

        for (auto startTarget : startTargets) {
            auto startTargetNode = graph.Get(startTarget.Id);
            startTargetNode->State.SetReachable(true);

            if (startTarget.IsModuleTarget) {
                continue;
            }

            for (auto dep : startTargetNode.Edges()) {
                if (IsMakeFileType(dep.To()->NodeType)) {
                    dep.To()->State.SetReachable(true);
                }
            }
        }
    }
}
