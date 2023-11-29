#pragma once

#include "iter_direct_peerdir.h"
#include "query.h"

struct TCollectorEntryStatsData: TEntryStatsData {
    TCollectorEntryStatsData(bool isFile = false)
        : TEntryStatsData{isFile}
        , Started{false}
    {}

    bool Started;
};

// Postfix DFS traversal through the modules inside dep graph connected by direct peerdir dependency.
// TPeersCollector must provide the following members:
// class TUserProvidedPeersCollector {
//     // Type of state item used during traversal. Collector can access and modify state item of the
//     // module which was started but not yet finished. TGraphIteratorStateItemBase can be used if no
//     // extra information needed to be preserved during module peers collecting or
//     // TGraphIteratorStateItem<TStateData> if extra information is required.
//     using TStateItem = ...
//
//     // Called for each module before any other methods of the collector. Can be used to initialize
//     // data stored in traversal stack for the module being processed. If this function returns false
//     // no collection will be performed for the item and for those direct or indirect peers which are
//     // unreachable from other modules being collected (such module will never appear as parent in
//     // Collect member function or in Finish member fynction but can appear as peer in Collect).
//     bool Start(TStateItem& parentItem);
//
//     // Called after all peers of the module have been collected. State item for this module is going
//     // to be destroyed and all important information should be saved by this method somewhere.
//     void Finish(TStateItem& parentItem);
//
//     // Called on each parentItem peer. There is strong guarantie that Start have been called for both
//     // parent and peer module and Finish have been called for peer module but not parent.
//     void Collect(TStateItem& parentItem, TConstDepNodeRef peerNode);
// }
template <typename TPeersCollector>
class TPeerCollectingVisitor: public TDirectPeerdirsVisitor<TVisitorStateItem<TCollectorEntryStatsData>, typename TPeersCollector::TStateItem> {
public:
    using TBase = TDirectPeerdirsVisitor<TVisitorStateItem<TCollectorEntryStatsData>, typename TPeersCollector::TStateItem>;
    using TState = typename TBase::TState;

    TPeerCollectingVisitor(TPeersCollector& collector)
        : Collector{collector} {
    }

    bool Enter(TState& state) {
        if (!TBase::Enter(state)) {
            return false;
        }
        if (IsModule(state.Top())) {
            Y_ASSERT(this->CurEnt); // TBase::Enter returns false in case when CurEnt is reset to nullptr
            this->CurEnt->Started = Collector.Start(state.Top());
            return this->CurEnt->Started;
        }
        return true;
    }

    void Leave(TState& state) {
        if (this->CurEnt && std::exchange(this->CurEnt->Started, false)) {
            Y_ASSERT(IsModule(state.Top()));
            Collector.Finish(state.Top());
        }
        TBase::Leave(state);
    }

    void Left(TState& state) {
        TBase::Left(state);
        if (this->CurEnt && this->CurEnt->Started && IsDirectPeerdirDep(state.Top().CurDep())) {
            Collector.Collect(state.Top(), state.Top().CurDep().To());
        }
    }

protected:
    TPeersCollector& GetCollector() noexcept {return Collector;}

private:
    TPeersCollector& Collector;
};

template <typename TPeersCollector>
void CollectPeers(const TDepGraph& graph, const TVector<TConstDepNodeRef>& startModules, TPeersCollector& collector) {
    TPeerCollectingVisitor visitor{collector};
    IterateAll(graph, startModules, visitor);
}

template<typename TModuleDepsCollector>
class TModuleDepsCollectingVisitor: public TPeerCollectingVisitor<TModuleDepsCollector> {
public:
    using TBase = TPeerCollectingVisitor<TModuleDepsCollector>;
    using TState = typename TBase::TState;
    using TStateItem = typename TModuleDepsCollector::TStateItem;

    TModuleDepsCollectingVisitor(TModuleDepsCollector& collector): TBase{collector} {}

    bool AcceptDep(TState& state) {
        return TBase::AcceptDep(state) && !IsTooldirDep(state.Top().CurDep());
    }

    void Left(TState& state) {
        TBase::Left(state);
        if (IsDirectToolDep(state.Top().CurDep())) {
            const auto curentModule = state.FindRecent([](auto& item) { return IsModule(item); });
            Y_ASSERT(curentModule != state.end());
            const auto moduleEntry = this->Nodes.find(curentModule->Node().Id());
            if (moduleEntry != this->Nodes.end() && moduleEntry->second.Started) {
                this->GetCollector().CollectTool(*curentModule, state.Top(), state.Top().CurDep().To());
            }
        }
    }
};
