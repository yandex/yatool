#pragma once

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/options/roots_options.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <library/cpp/containers/absl_flat_hash/flat_hash_set.h>
#include <util/generic/hash_set.h>
#include <util/folder/path.h>

#include "vars.h"

class TNodeListStore {
private:
    TDeque<TUniqVector<TNodeId>> Lists;

public:
    class TListId;

    TNodeListStore();

    TListId CreateList();

    const TUniqVector<TNodeId>& GetList(TListId id) const;
    void AddToList(TListId& id, TNodeId node);
    void MergeLists(TListId& dest, TListId& src);
};

class TNodeListStore::TListId {
private:
    size_t Idx;

    static constexpr size_t SHARED_MASK = ~(~size_t{0} >> 1);

public:
    explicit constexpr TListId(size_t idx = 0 | SHARED_MASK) noexcept
        : Idx{idx} {
    }

    constexpr TListId Share() noexcept {
        Idx |= SHARED_MASK;
        return TListId{Idx};
    }

    constexpr bool IsShared() const noexcept {
        return (Idx & SHARED_MASK) != 0;
    }

    constexpr size_t ToIndex() const noexcept {
        return Idx & ~SHARED_MASK;
    }

    constexpr bool operator==(TListId other) const noexcept {
        return Idx == other.Idx;
    }
    constexpr bool operator!=(TListId other) const noexcept {
        return Idx != other.Idx;
    }
};

struct TModuleNodeIds {
    absl::flat_hash_set<TNodeId> DepCommandIds;
    THashSet<TNodeId> LocalPeers;
    TNodeListStore::TListId GlobalSrcsIds;
    TNodeListStore::TListId UniqPeers;
    TNodeListStore::TListId Tools;
    TNodeListStore::TListId ManagedDirectPeers;
};

class TModuleNodeLists {
public:
    TModuleNodeLists() noexcept = default;
    TModuleNodeLists(const TNodeListStore& store, const TModuleNodeIds& ids) noexcept
        : Store_{&store}, Ids_{&ids}
    {}

    const absl::flat_hash_set<TNodeId>& DepCommandIds() const noexcept{
        return Ids_->DepCommandIds;
    }

    const THashSet<TNodeId>& LocalPeers() const noexcept {
        return Ids_->LocalPeers;
    }

    const TUniqVector<TNodeId>& GlobalSrcsIds() const noexcept {
        return Store_->GetList(Ids_->GlobalSrcsIds);
    }

    const TUniqVector<TNodeId>& UniqPeers() const noexcept {
        return Store_->GetList(Ids_->UniqPeers);
    }

    const TUniqVector<TNodeId>& Tools() const noexcept {
        return Store_->GetList(Ids_->Tools);
    }

    const TUniqVector<TNodeId>& ManagedDirectPeers() const noexcept {
        return Store_->GetList(Ids_->ManagedDirectPeers);
    }

private:
    const TNodeListStore* Store_{nullptr};
    const TModuleNodeIds* Ids_{nullptr};
};

struct TRealPathRoots {
    TFsPath SourceRoot;
    TFsPath BuildRoot;
    bool Normalized;

    TRealPathRoots() = default;

    TRealPathRoots(const TRootsOptions& roots)
        : SourceRoot(roots.SourceRoot)
        , BuildRoot(roots.BuildRoot)
        , Normalized(roots.NormalizeRealPath)
    {
    }

    bool operator==(const TRealPathRoots& roots) const {
        return SourceRoot == roots.SourceRoot && BuildRoot == roots.BuildRoot && Normalized == roots.Normalized;
    }
};

struct TGlobalVars {
public:
    bool IsVarsComplete() const {
        return VarsComplete;
    }

    void SetVarsComplete() {
        Y_ASSERT(!IsVarsComplete());
        VarsComplete = true;
    }

    bool IsPathsNoChanged(const TRealPathRoots& roots) const {
        return Roots == roots;
    }

    void ResetRoots(const TRealPathRoots& roots) {
        Vars.clear();
        VarsComplete = false;
        Roots = roots;
    }

    TVars& GetVars() {
        return Vars;
    }

    const TVars& GetVars() const {
        return Vars;
    }

private:
    TVars Vars;
    TRealPathRoots Roots;
    bool VarsComplete = false;
};

struct TTransitiveModuleInfo {
    TModuleNodeIds NodeIds;
    TGlobalVars GlobalVars;
};
