#include "name_store.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/diag/dbg.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/ysaveload.h>

static inline ui64 GoodHash(const TStringBuf& s) noexcept {
    return CityHash64(s.data(), s.size());
}

ui32 TNameStore::Add(TStringBuf name) {
    auto key = GoodHash(name);

    {
        const auto it = Name2Id.find(key);

        if (it != Name2Id.end()) {
            return it->second;
        }
    }

    size_t currentId = Names.size();
    auto sName = Pool->Append(name);

    Name2Id[key] = currentId;
    Names.push_back(sName);

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
    TNameToId::const_iterator it = Name2Id.find(GoodHash(name));

    if (it != Name2Id.end()) {
        return it->second;
    }

    return 0;
}

bool TNameStore::Has(TStringBuf name) const {
    return Name2Id.find(GoodHash(name)) != Name2Id.end();
}

bool TNameStore::CheckId(ui32 id) const {
    if (Y_UNLIKELY(!id)) {
        throw yexception() << "GetName: internal error: trying to get name for id = 0\n";
    }
    if (Y_UNLIKELY(id >= Names.size())) {
        return false;
    }
    return true;
}

TNameStore::~TNameStore() {
}

void TNameStore::Clear() {
    IMemoryPool::Construct().Swap(Pool);
    Name2Id.clear();
    Names.clear();
    Names.push_back(Pool->Append(TStringBuf()));
    Blob = TBlob();
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
    ::Save(out, Names);
}

void TNameStore::Load(TBlob& multi) {
    TSubBlobs blobs(multi);

    LoadSingleBlob(blobs[0]);
}

void TNameStore::LoadSingleBlob(TBlob& blob) {
    // to test error condition while cache loading
    // throw yexception() << "bambaleylo!";

    Clear();

    Blob = blob;

    {
        TMemoryInput mi(Blob.Data(), Blob.Size());

        ::Load(&mi, Names);
    }

    for (size_t i = 1; i < Names.size(); ++i) {
        Name2Id[GoodHash(Names[i])] = i;
    }
}

TStringBuf TNameStore::GetStringBufName(ui32 id) const {
    if (CheckId(id)) {
        return Names[id];
    }

    return {};
}
