#include "value_storage.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/digest/city.h>
#include <util/ysaveload.h>

namespace {
    ui64 Hash(TValueStorage::TValue value) {
        auto hash = CityHash64(reinterpret_cast<const char*>(&value.first), sizeof value.first);
        hash = CityHash64WithSeed(reinterpret_cast<const char*>(value.second.data()), value.second.size(), hash);
        return hash;
    }

    auto ViewToValue(IMemoryPool::TView view) {
        TValueStorage::TValue result;
        Y_ASSERT(view.size() >= sizeof result.first);
        memcpy(&result.first, view.data(), sizeof result.first);
        result.second = std::as_bytes(std::span(view.data(), view.size())).subspan(sizeof result.first);
        return result;
    }
}

//
//
//

template <>
class TSerializer<IMemoryPool::TView> {
    using TSize = ui32;
public:
    static inline void Save(IOutputStream* rh, const IMemoryPool::TView& s) {
        ::Save(rh, (TSize)s.size());
        ::SavePodArray(rh, s.data(), s.size());
    }

    static inline void Load(IInputStream* rh, IMemoryPool::TView& s) {
        TSize len;
        const void* ptr;

        rh->Load(&len, sizeof(len));

        if (((TMemoryInput*)rh)->Next(&ptr, len) != len) {
            ythrow yexception() << "malformed len " << len;
        }

        s = std::span((const std::byte*)ptr, len);
    }
};

//
//
//

TValueStorage::TValueStorage() {
    Clear();
}

TValueStorage::~TValueStorage() {
}

NPolexpr::TConstId TValueStorage::Put(TValue value) {

    auto hash = ::Hash(value);

    const auto it = Index.find(hash);
    if (it != Index.end())
        return NPolexpr::TConstId(ToUnderlying(EStorageType::Pool), it->second);

    auto dst = (std::byte*)Pool->Allocate(sizeof value.first + value.second.size());
    memcpy(dst, &value.first, sizeof value.first);
    memcpy(dst + sizeof value.first, value.second.data(), value.second.size_bytes());

    size_t newId = Values.size();
    Index[hash] = newId;
    Values.push_back(IMemoryPool::TView{dst, sizeof value.first + value.second.size()});

    return NPolexpr::TConstId(ToUnderlying(EStorageType::Pool), newId);

}

TValueStorage::TValue TValueStorage::Get(NPolexpr::TConstId id) const {
    Y_ASSERT(id.GetStorage() == ToUnderlying(EStorageType::Pool));
    Y_ASSERT(0 < id.GetIdx() && id.GetIdx() < Values.size());
    auto buf = Values[id.GetIdx()];
    return ViewToValue(buf);
}

void TValueStorage::Save(TMultiBlobBuilder& builder) const {
    TBuffer buffer;
    {
        TBufferOutput bo(buffer);
        Save(&bo);
    }
    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBuffer(buffer)));
}

void TValueStorage::Save(IOutputStream* out) const {
    ::Save(out, Values);
}

void TValueStorage::Load(TBlob& multi) {
    TSubBlobs blobs(multi);
    LoadSingleBlob(blobs[0]);
}

void TValueStorage::LoadSingleBlob(TBlob& blob) {
    Clear();
    Blob = blob;
    {
        TMemoryInput mi(Blob.Data(), Blob.Size());
        ::Load(&mi, Values);
    }
    for (size_t i = 1; i < Values.size(); ++i) {
        Index[Hash(ViewToValue(Values[i]))] = i;
    }
}

void TValueStorage::Clear() {
    IMemoryPool::Construct().Swap(Pool);
    Index.clear();
    Values.clear();
    Values.push_back(Pool->Append(IMemoryPool::TView()));
    Blob = TBlob();
}
