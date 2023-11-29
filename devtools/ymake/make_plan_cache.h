#pragma once

#include "conf.h"
#include "json_visitor.h"

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/make_plan/make_plan.h>
#include <devtools/ymake/symbols/name_store.h>

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

    struct TConversionContext;
}

class TMakeNodeSavedState {
public:
    struct TCacheId {
        NCache::TCached Id;
        bool StrictInputs;

        bool operator==(const TCacheId& rhs) const;
    };

private:
    TMakeNodeDescription<NCache::TCached, NCache::TJoinedCached, NCache::TJoinedCachedCommand> CachedNode;
    NCache::TCached InvalidationId;
#if !defined (NEW_UID_IMPL)
    NCache::TCached PartialMatchId;
#endif
    bool StrictInputs;

public:
    TMakeNodeSavedState() = default;
    TMakeNodeSavedState(const TMakeNode& node, const TStringBuf& nodeName, const TStringBuf& nodeCacheUid, const TStringBuf& nodeRenderId, const TBuildConfiguration& conf, NCache::TConversionContext& context);
    void Restore(NCache::TConversionContext& context, TMakeNode* result) const;

#if !defined (NEW_UID_IMPL)
    Y_SAVELOAD_DEFINE(CachedNode, InvalidationId, PartialMatchId, StrictInputs);
#else
    Y_SAVELOAD_DEFINE(CachedNode, InvalidationId, StrictInputs);
#endif

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
    TFsPath CachePath;

    TNameStore Names;

    TDeque<TMakeNodeSavedState> RestoredNodes;
    TDeque<TMakeNodeSavedState> AddedNodes;

    TRestoredNodesMap FullMatchMap;
    // TODO: Remove after switching to new UIDs implementation.
    TRestoredNodesMap PartialMatchMap;

public:
    explicit TMakePlanCache(const TBuildConfiguration& conf);

    bool RestoreByCacheUid(const TStringBuf& uid, TMakeNode* result);

    // Correspondence "RenderId <-> Text(Rendered Command)" is biunique
    bool RestoreByRenderId(const TStringBuf& renderId, TMakeNode* result);

    void AddRenderedNode(const TMakeNode& newNode, TStringBuf name, TStringBuf cacheUid, TStringBuf renderId);

    void LoadFromFile();
    TFsPath SaveToFile();

    void LoadFromContext(const TString& context);
    TString SaveToContext();

    TString GetStatistics() const;

    TFsPath GetCachePath() const {
        return CachePath;
    }

    NStats::TJsonCacheStats Stats{"JSON cache stats"};

private:
    bool RestoreNode(const TStringBuf& id, bool partialMatch, TMakeNode* result);

    void Load(TBlob& namesBlob, TBlob& nodesBlob);
    void Save(TMultiBlobBuilder& builder);
};
