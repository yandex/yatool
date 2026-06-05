#include "name_store.h"

#include <devtools/ymake/common/npath.h>

#include <util/generic/buffer.h>
#include <util/stream/buffer.h>
#include <util/ysaveload.h>

static inline ui64 GoodHash(const TStringBuf& s) noexcept {
    return CityHash64(s.data(), s.size());
}

ui32 TNameStore::Add(TStringBuf name) {
    auto key = GoodHash(name);

    {
        const auto it = Name2Id_.find(key);

        if (it != Name2Id_.end()) {
            return it->second;
        }
    }

    size_t currentId = Names_.size();
    auto sName = Pool_->Append(name);

    Name2Id_[key] = currentId;
    Names_.push_back(sName);

    return currentId;
}

ui32 TNameStore::GetId(TStringBuf name) const {
    size_t result = GetIdNx(name);

    if (result) {
        return result;
    }

    throw yexception() << "No id available for '" << name << "'\n";
}

ui32 TNameStore::GetIdNx(TStringBuf name) const {
    TNameToId::const_iterator it = Name2Id_.find(GoodHash(name));

    if (it != Name2Id_.end()) {
        return it->second;
    }

    return 0;
}

bool TNameStore::Has(TStringBuf name) const {
    return Name2Id_.find(GoodHash(name)) != Name2Id_.end();
}

bool TNameStore::CheckId(ui32 id) const {
    if (Y_UNLIKELY(!id)) {
        throw yexception() << "GetName: internal error: trying to get name for id = 0\n";
    }
    if (Y_UNLIKELY(id >= Names_.size())) {
        return false;
    }
    return true;
}

TNameStore::~TNameStore() {
}

void TNameStore::Clear() {
    IMemoryPool::Construct().Swap(Pool_);
    Name2Id_.clear();
    Names_.clear();
    Names_.push_back(Pool_->Append(TStringBuf()));
    Blob_ = TBlob();
}

template <>
class TSerializer<TStringBuf> {
    using TSize = ui32;
public:
    static inline void Save(IOutputStream* rh, const TStringBuf& s) {
        // save last zero
        size_t length = s.length() + 1;
        ::Save(rh, (TSize)length);
        ::SavePodArray(rh, s.data(), length);
    }

    static inline void Load(IInputStream* rh, TStringBuf& s) {
        TSize len;
        const void* ptr;

        rh->Load(&len, sizeof(len));

        if (len < 1 || ((TMemoryInput*)rh)->Next(&ptr, len) != len) {
            ythrow yexception() << "malformed len " << len;
        }

        if (((const char*)ptr)[len - 1] != 0) {
            ythrow yexception() << "malformed terminator";
        }

        s = TStringBuf((const char*)ptr, len - 1);
    }
};

void TNameStore::Save(TMultiBlobBuilder& builder) const {
    TBuffer buffer;

    {
        TBufferOutput bo(buffer);

        Save(&bo);
    }

    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBuffer(buffer)));
}

void TNameStore::Save(IOutputStream* out) const {
    ::Save(out, Names_);
}

void TNameStore::Load(TBlob& multi) {
    TSubBlobs blobs(multi);

    LoadSingleBlob(blobs[0]);
}

void TNameStore::LoadSingleBlob(TBlob& blob) {
    // to test error condition while cache loading
    // throw yexception() << "bambaleylo!";

    Clear();

    Blob_ = blob;

    {
        TMemoryInput mi(Blob_.Data(), Blob_.Size());

        ::Load(&mi, Names_);
    }

    for (size_t i = 1; i < Names_.size(); ++i) {
        Name2Id_[GoodHash(Names_[i])] = i;
    }
}

TStringBuf TNameStore::GetStringBufName(ui32 id) const {
    if (CheckId(id)) {
        return Names_[id];
    }

    return {};
}
