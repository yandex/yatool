#pragma once

#include "path_resolver.h"

#include <util/digest/city.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>

#include <unordered_map>

class TResolveCacheKey {
public:
    TResolveCacheKey(const TString& name, ui64 planKey)
        : Hash_(CombineHashes(CityHash64(name), IntHash(planKey)))
        , Name_(name)
    {
    }

    TResolveCacheKey(TResolveCacheKey&&) = default;

    size_t Hash() const {
        return Hash_;
    }

    bool operator==(const TResolveCacheKey& other) const {
         return Hash_ == other.Hash_ && Name_ == other.Name_;
    }

private:
    ui64 Hash_;
    TString Name_;
};


template <>
struct THash<TResolveCacheKey> {
    size_t operator()(const TResolveCacheKey& key) const {
        return key.Hash();
    }
};

struct TResolveCacheValue {
    EResolveStatus Status;///< Resolve status
    TResolveFile View;///< Resolve data, filled only if Status == RESOLVE_SUCCESS, else empty
    TString UnresolvedName;///< Unresolved name, filled only if Status != RESOLVE_SUCCESS, else empty

    explicit TResolveCacheValue(EResolveStatus status, TResolveFile view, TStringBuf unresolvedName = "")
        : Status(status)
        , View(view)
        , UnresolvedName(unresolvedName)
    {}

    bool IsSuccess() const {
        return Status == RESOLVE_SUCCESS;
    }
};

class TResolveCacheImpl : TNonCopyable {
    using TStore = THashMap<TResolveCacheKey, TResolveCacheValue>;
    using TValue = TStore::value_type;
    using TIterator = TStore::iterator;

public:
    TIterator Put(TResolveCacheKey&& key, TResolveCacheValue&& value) {
        auto [it, _] = Store_.emplace(std::move(key), std::move(value));
        return it;
    }

    TIterator Get(const TResolveCacheKey& key) {
        return Store_.find(key);
    }

    bool Found(TIterator it) const {
        return it != Store_.end();
    }

private:
    TStore Store_;
};


struct TResolveCache : TSimpleSharedPtr<TResolveCacheImpl> {
    TResolveCache() {
        Reset(new TResolveCacheImpl());
    }
};

class TResolveCaches : TNonCopyable {
    using TStore = THashMap<ui32, TResolveCache>;

public:
    /// Get Cache for specified id
    /// If there were no cache already, it will be created
    TResolveCache Get(ui32 id) {
        return Store_[id];
    }

    /// Drop entire cache for id
    /// This is useful to save memory if node asocciated with cache popped from visitor stack
    ///
    /// Impirtant! Don't call Drop until you absolutely sure that all resolvers for
    /// this module are already destroyed.
    void Drop(ui32 id) {
        Store_.erase(id);
    }

private:
    TStore Store_;
};
