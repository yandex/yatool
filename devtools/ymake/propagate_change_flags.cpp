#include "propagate_change_flags.h"

#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/compact_graph/query.h>

#include <devtools/ymake/diag/trace.h>

namespace NPropagateChangeFlags {
    constexpr TNodeId InvalidLoop = static_cast<TNodeId>(-1);

    struct TVisitorData : public TVisitorStateItem<TEntryStatsData> {
        using TItemDebug = TVisitorStateItemBaseDebug;

        TNodeId LoopId = InvalidLoop;

        using TVisitorStateItem::TVisitorStateItem;
    };

    struct TStateItem : public TGraphIteratorStateItemBase<false> {
        using TGraphIteratorStateItemBase::TGraphIteratorStateItemBase;
        bool WasEntered = false;
        TNodeId ModuleNode = 0;
    };

    class TVisitor: public TDirectPeerdirsVisitor<TVisitorData, TStateItem> {
    public:
        using TBase = TDirectPeerdirsVisitor<TVisitorData, TStateItem>;

    public:
        TVisitor(TDepGraph& graph, const TGraphLoops& loops)
            : Graph_(graph)
            , Loops_(loops)
        {
        }

        bool AcceptDep(TState& state) {
            const auto& dep = state.NextDep();

            if (IsPropertyFileDep(dep)) {
                return true;
            }
            if (IsTooldirDep(dep)) {
                return false;
            }
            if (!IsLoopGenDep(dep)) {
                return false;
            }

            return TBase::AcceptDep(state);
        }

        bool Enter(TState& state) {
            bool fresh = TBase::Enter(state);
            TStateItem& stateItem = state.Top();
            stateItem.WasEntered = fresh;
            EnsureLoopId(stateItem);
            SetModuleNode(stateItem, state);

            if (fresh && state.TopNode()->NodeType == EMNT_NonParsedFile) {
                auto moduleNode = Graph_[stateItem.ModuleNode];
                state.TopNode()->State.PropagatePartialChangesFrom(moduleNode->State);
            }

            return fresh;
        }

        void EnsureLoopId(TStateItem& stateItem) {
            if (!stateItem.WasEntered) {
                Y_ASSERT(CurEnt->LoopId != InvalidLoop);
                return;
            }

            auto it = Loops_.Node2Loop.find(stateItem.Node().Id());
            if (it != Loops_.Node2Loop.end()) {
                CurEnt->LoopId = it->second;
                Y_ASSERT(CurEnt->LoopId != 0);
            } else {
                CurEnt->LoopId = 0;
            }
        }

        void SetModuleNode(TStateItem& stateItem, TState& state) {
            if (IsModuleType(stateItem.Node()->NodeType)) {
                stateItem.ModuleNode = stateItem.Node().Id();
            } else {
                if (state.Size() > 1) {
                    stateItem.ModuleNode = state.Parent()->ModuleNode;
                }
            }
        }

        void Leave(TState& state) {
            TBase::Leave(state);

            auto chldStateItem = state.Top();

            auto prntNode = state.ParentNode();
            auto chldNode = state.TopNode();

            bool justFinishedChild = chldStateItem.WasEntered;
            bool makeChildRecursive = justFinishedChild;
            bool propagateChanges = true;

            if (state.Size() < 2) {
                propagateChanges = false;

            } else {
                TVisitorData* chldData = VisitorEntry(chldStateItem);
                TVisitorData* prntData = VisitorEntry(*++state.begin());

                TNodeId chldLoop = chldData->LoopId;
                TNodeId prntLoop = prntData->LoopId;

                bool inSameLoop = (chldLoop && chldLoop == prntLoop);
                bool justFinishedLoop = (chldLoop && chldLoop != prntLoop) && justFinishedChild;

                if (inSameLoop) {
                    // Do nothing. All loop nodes will be processed at once when the loop is finished.
                    makeChildRecursive = false;
                    propagateChanges = false;
                }

                if (justFinishedLoop) {
                    ProcessLoop(chldLoop);

                    // ProcessLoop already set Recursive on all loop nodes
                    makeChildRecursive = false;
                }

            }

            if (makeChildRecursive) {
                chldNode->State.SetChangedFlagsRecursiveScope();
            }

            if (propagateChanges) {
                prntNode->State.PropagateChangesFrom(chldNode->State);
            }
        }

    private:
        void ProcessLoop(TNodeId loopId) {
            TNodeState loopState;
            loopState.SetLocalChanges(false, false);

            for (TNodeId id : Loops_[loopId]) {
                loopState.PropagatePartialChangesFrom(Graph_.Get(id)->State);
            }

            loopState.SetChangedFlagsRecursiveScope();

            for (TNodeId id : Loops_[loopId]) {
                TNodeState& nodeState = Graph_.Get(id)->State;
                nodeState.PropagateChangesFrom(loopState);
                nodeState.SetChangedFlagsRecursiveScope();
            }
        }

        TDepGraph& Graph_;
        const TGraphLoops& Loops_;
    };
}

void PropagateChangeFlags(TDepGraph& graph, const TGraphLoops& loops, TVector<TTarget>& startTargets) {
    NPropagateChangeFlags::TVisitor visitor{graph, loops};
    IterateAll(graph, startTargets, visitor);

    bool hasStructuralChanges = false;
    bool hasContentChanges = false;
    for (auto target : startTargets) {
        auto node = graph.Get(target.Id);
        hasStructuralChanges |= node->State.HasRecursiveStructuralChanges();
        hasContentChanges |= node->State.HasRecursiveContentChanges();
    }

    NEvent::TGraphChanges ev;
    ev.SetHasStructuralChanges(hasStructuralChanges);
    ev.SetHasContentChanges(hasContentChanges);
    FORCE_TRACE(U, ev);
}
