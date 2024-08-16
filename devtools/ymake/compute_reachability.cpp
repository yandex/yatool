#include "compute_reachability.h"

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/query.h>

namespace NComputeReachability {
    using TStateItem = TGraphIteratorStateItemBase<false>;

    class TVisitor: public TDirectPeerdirsVisitor<TEntryStats, TStateItem> {
    public:
        using TBase = TDirectPeerdirsVisitor<TEntryStats, TStateItem>;

        bool Enter(TState& state) {
            bool fresh  = TBase::Enter(state);

            if (fresh) {
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
