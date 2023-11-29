#include "transitive_state.h"

TNodeListStore::TNodeListStore() {
    // Default constructed TListId is a shared link to this empty list thus any midification through it
    // will fork empty list and fill it with necessary data.
    Lists.push_back({});
}

TNodeListStore::TListId TNodeListStore::CreateList() {
    TListId id{Lists.size()};
    Lists.push_back({});
    return id;
}

const TUniqVector<TNodeId>& TNodeListStore::GetList(TListId id) const {
    return Lists.at(id.ToIndex());
}

void TNodeListStore::AddToList(TListId& id, TNodeId node) {
    if (id.IsShared()) {
        const auto& srcList = GetList(id);
        if (srcList.has(node)) {
            return;
        }
        auto detached = srcList;
        detached.Push(node);
        id = TListId{Lists.size()};
        Lists.push_back(std::move(detached));
    } else {
        Lists.at(id.ToIndex()).Push(node);
    }
}

void TNodeListStore::MergeLists(TListId& dest, TListId& src) {
    const auto& srcLst = Lists.at(src.ToIndex());
    auto& dstLst = Lists.at(dest.ToIndex());

    if (dstLst.empty()) {
        dest = src.Share();
        return;
    }

    if (dest.IsShared()) {
        const auto it = FindIf(srcLst.begin(), srcLst.end(), [&dstLst](TNodeId item) { return !dstLst.has(item); });
        if (it == srcLst.end()) {
            return;
        }

        auto detached = dstLst;
        ForEach(it, srcLst.end(), [&detached](TNodeId item) { detached.Push(item); });
        dest = TListId{Lists.size()};
        Lists.push_back(std::move(detached));
    } else {
        srcLst.AddTo(dstLst);
    }
}
