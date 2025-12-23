#pragma once

#include "conf.h"
#include "json_visitor.h"

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/make_plan/make_plan.h>
#include <devtools/ymake/symbols/name_store.h>

#include <mutex>
#include <util/ysaveload.h>
#include <util/folder/path.h>
#include <util/generic/deque.h>
#include <util/generic/fwd.h>
#include <util/generic/hash.h>
#include <util/generic/maybe.h>
#include <util/generic/vector.h>
#include <util/system/types.h>

class TJSONVisitor;
class TYMake;
class TBlob;
class TMultiBlobBuilder;

namespace NCache {
    using TCached = ui32;
    using TJoinedCached = TVector<TCached>;
    struct TJoinedCachedCommand {
        ui8 Tag;
        TJoinedCached Data;
        Y_SAVELOAD_DEFINE(Tag, Data);
    };

    class TConversionContext {
    private:
        TNameStore& Names_;
        bool StoreInputs_;
        const TMakeNode* RefreshedMakeNode_;///< MakeNode with refreshed UIDs
        std::function<ui32(const TStringBuf&, TNameStore&)> IdGetter_;

    public:
        explicit TConversionContext(TNameStore& names, bool storeInputs, std::function<ui32(const TStringBuf&, TNameStore&)> idGetter = [](const TStringBuf& name, TNameStore& names) -> ui32 { return names.Add(name); })
                : Names_(names)
                , StoreInputs_(storeInputs)
                , IdGetter_(idGetter)
        {
        }

        template <typename TStrType>
        void Convert(const TCached& src, TStrType& dst) const;

        TStringBuf GetBuf(const TCached& src) const;

        template <typename TStrType>
        void Convert(TStrType&& src, TCached& dst);

        void Convert(const TOriginal& src, TJoinedCached& dst);

        void Convert(const TVector<TOriginal>& src, TJoinedCached& dst);

        void Convert(const TJoinedCommand& src, TJoinedCachedCommand& dst);

        void Convert(const TJoinedCached& src, TOriginal& dst);

        void Convert(const TJoinedCached& src, TVector<TOriginal>& dst);

        void Convert(const TJoinedCachedCommand& src, TJoinedCommand& dst);

        template <typename TSrc, typename TDst>
        void Convert(const TVector<TSrc>& src, TVector<TDst>& dst);

        template <typename TSrc, typename TDst>
        void Convert(const TMaybe<TSrc>& src, TMaybe<TDst>& dst);

        template <typename TSrc, typename TDst>
        void Convert(const TKeyValueMap<TSrc>& src, TKeyValueMap<TDst>& dst);

        template <typename TSrc, typename TJoinedSrc, typename TDst, typename TJoinedDst>
        void Convert(const TMakeCmdDescription<TSrc, TJoinedSrc>& src, TMakeCmdDescription<TDst, TJoinedDst>& dst);

        template <
            typename TSrc, typename TJoinedSrc, typename TJoinedCmdSrc,
            typename TDst, typename TJoinedDst, typename TJoinedCmdDst
        >
        void Convert(
            const TMakeNodeDescription<TSrc, TJoinedSrc, TJoinedCmdSrc>& src,
            TMakeNodeDescription<TDst, TJoinedDst, TJoinedCmdDst>& dst
        );

        const TMakeNode* GetRefreshedMakeNode() const {
            return RefreshedMakeNode_;
        }

        void SetRefreshedMakeNode(const TMakeNode* node) {
            RefreshedMakeNode_ = node;
        }

        bool GetStoreInputs() const {
            return StoreInputs_;
        }

        TNameStore& GetNames() {
            return Names_;
        }

        const TNameStore& GetNames() const {
            return Names_;
        }
    };
}

using TMakeCmdCached = TMakeCmdDescription<NCache::TCached, NCache::TJoinedCachedCommand>;
using TMakeNodeCached = TMakeNodeDescription<NCache::TCached, NCache::TJoinedCached, NCache::TJoinedCachedCommand>;

class TMakeNodeSavedState {
public:
    struct TCacheId {
        NCache::TCached Id;
        bool StrictInputs;

        bool operator==(const TCacheId& rhs) const;
    };

private:
    TMakeNodeCached CachedNode;
    NCache::TCached InvalidationId;
    NCache::TCached PartialMatchId;
    bool StrictInputs;

public:
    TMakeNodeSavedState() = default;
    TMakeNodeSavedState(const TMakeNode& node, const TStringBuf& nodeName, const TStringBuf& nodeCacheUid, const TStringBuf& nodeRenderId, const TBuildConfiguration& conf, NCache::TConversionContext& context);
    void Restore(NCache::TConversionContext& context, TMakeNode* result) const;
    TVector<NCache::TOriginal> RestoreLateOuts(NCache::TConversionContext& context) const;
    void WriteAsJson(NYMake::TJsonWriter& writer, const NCache::TConversionContext* conversionContext) const;

    Y_SAVELOAD_DEFINE(CachedNode, InvalidationId, PartialMatchId, StrictInputs);

public:
    friend class TMakePlanCache;
};

template <>
struct THash<TMakeNodeSavedState::TCacheId> {
    size_t operator()(const TMakeNodeSavedState::TCacheId& cacheId) const {
        return CombineHashes(cacheId.Id, static_cast<ui32>(cacheId.StrictInputs));
    }
};

TMd5Sig JsonConfHash(const TBuildConfiguration& conf);

class TMakePlanCache {
private:
    using TRestoredNodesMap = THashMap<TMakeNodeSavedState::TCacheId, std::reference_wrapper<TMakeNodeSavedState>>;

    const TBuildConfiguration& Conf;

    const bool LoadFromCache;
    const bool SaveToCache;
    const bool LockCache;
    TFsPath CachePath;

    TNameStore Names;

    TDeque<TMakeNodeSavedState> RestoredNodes;
    TDeque<TMakeNodeSavedState> AddedNodes;

    TRestoredNodesMap FullMatchMap;
    // TODO: Remove after switching to new UIDs implementation.
    TRestoredNodesMap PartialMatchMap;

    THolder<NCache::TConversionContext> ConversionContext_;

    TAdaptiveLock ContextLock;

public:
    explicit TMakePlanCache(const TBuildConfiguration& conf);
    ~TMakePlanCache();

    bool RestoreByCacheUid(const TStringBuf& uid, TMakeNode* result);
    const TMakeNodeSavedState* GetCachedNodeByCacheUid(const TStringBuf& uid);
    NCache::TConversionContext& GetConversionContext(const TMakeNode* refreshedMakeNode = nullptr);
    NCache::TConversionContext GetConstConversionContext(const TMakeNode* refreshedMakeNode = nullptr);

    // Correspondence "RenderId <-> Text(Rendered Command)" is biunique
    bool RestoreByRenderId(const TStringBuf& renderId, TMakeNode* result);

    void AddRenderedNode(const TMakeNode& newNode, TStringBuf name, TStringBuf cacheUid, TStringBuf renderId);

    bool LoadFromFile();
    TFsPath SaveToFile();

    void LoadFromContext(const TString& context);
    TString SaveToContext();

    TString GetStatistics() const;

    TFsPath GetCachePath() const {
        return CachePath;
    }

    std::unique_lock<TAdaptiveLock> LockContextIfNeeded();

    NStats::TJsonCacheStats Stats{"JSON cache stats"};

private:
    const TMakeNodeSavedState* GetCachedNode(const TStringBuf& id, bool partialMatch);
    bool RestoreNode(const TStringBuf& id, bool partialMatch, TMakeNode* result);

    void Load(TBlob& namesBlob, TBlob& nodesBlob);
    void Save(TMultiBlobBuilder& builder);
};
