#pragma once

#include "value.h"

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/polexpr/ids.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>
#include <library/cpp/containers/absl_flat_hash/flat_hash_map.h>

class IOutputStream;

class TValueStorage {
public:
    //using TId = ui32;
    using TType = EDataType;
    using TData = std::span<const std::byte>;
    using TValue = std::pair<TType, TData>;
    //static constexpr size_t IdBits = NPolexpr::TConstId::IDX_BITS;

public:
    TValueStorage();
    ~TValueStorage();

public:
    NPolexpr::TConstId Put(TValue value);
    TValue Get(NPolexpr::TConstId id) const;

    NPolexpr::TConstId Put(TType type, TData data) {
        return Put({type, data});
    }

public:
    void Save(IOutputStream* out) const;
    void Save(TMultiBlobBuilder& builder) const;
    void Load(TBlob& multi);
    void LoadSingleBlob(TBlob& blob);

private:
    using TIndex = absl::flat_hash_map<ui64, ui32, TIdentity>;
    using TValues = TVector<IMemoryPool::TView>;

    TAutoPtr<IMemoryPool> Pool;

    TBlob Blob;
    TIndex Index;
    TValues Values;

    void Clear();
};
