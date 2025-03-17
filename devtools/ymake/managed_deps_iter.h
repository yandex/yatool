#pragma once

#include <devtools/ymake/compact_graph/iter_direct_peerdir.h>
#include <devtools/ymake/module_restorer.h>
#include <devtools/ymake/module_store.h>

template<typename TDepRef>
bool IsReachableManagedDependency(const TRestoreContext& restoreContext, TDepRef dep) {
    if (!IsDirectPeerdirDep(dep)) {
        return true;
    }

    auto *parent = restoreContext.Modules.Get(dep.From()->ElemId);
    if (!parent->GetAttrs().RequireDepManagement) {
        return true;
    }

    auto *peer = restoreContext.Modules.Get(dep.To()->ElemId);
    if (!peer->GetAttrs().RequireDepManagement && !parent->GetAttrs().ConsumeNonManageablePeers) {
        return true;
    }
    return restoreContext.Modules.GetModuleNodeLists(parent->GetId()).UniqPeers().has(dep.To().Id());
}

template <typename TVisitorState = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
class TManagedPeerVisitor: public TDirectPeerdirsVisitor<TVisitorState, TIterStateItem, TIterState> {
public:
    using TBase = TDirectPeerdirsVisitor<TVisitorState, TIterStateItem, TIterState>;
    using typename TBase::TState;

    TManagedPeerVisitor(TRestoreContext restoreContext, TDependencyFilter Filter = TDependencyFilter{TDependencyFilter::SkipRecurses | TDependencyFilter::SkipAddincls})
        : TBase{Filter}, RestoreContext{restoreContext} {
    }

    bool AcceptDep(TState& state) {
        return TBase::AcceptDep(state) && IsReachableManagedDependency(RestoreContext, state.NextDep());
    }

protected:
    const TRestoreContext RestoreContext;
};

template <typename TVisitorState = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<true>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
using TManagedPeerConstVisitor = TManagedPeerVisitor<TVisitorState, TIterStateItem, TIterState>;
