#include "compute_reachability.h"

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/query.h>

namespace NComputeReachability {
    struct TStackData {
        bool IsIndirectPeerdir = false;
        bool IsDirStartTarget = false;
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
            const auto& st = state.Top();
            if (st.IsDirStartTarget || st.IsIndirectPeerdir) {
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
                if (!state.HasIncomingDep()) {
                    if (state.TopNode()->NodeType == EMNT_Directory) {
                        state.Stack().begin()->IsDirStartTarget = true;
                    }
                } else if (IsPeerdirDep(state.IncomingDep())) {
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

    void ComputeReachableNodes(TDepGraph& graph, TVector<TTarget>& startTargets) {
        TVisitor visitor;
        IterateAll(graph, startTargets, visitor);
    }
}
