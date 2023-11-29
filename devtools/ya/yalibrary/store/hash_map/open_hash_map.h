#pragma once

#include <util/system/filemap.h>
#include <util/digest/city.h>
#include <util/generic/function.h>

static constexpr size_t HASH_SIZE = sizeof(ui64);

class TOpenHashMapIter: public TThrRefBase {
public:
    TOpenHashMapIter(const char* ptr, ui64 size, ui64 contentSize)
        : Ptr(ptr)
        , End(ptr + size)
        , ContentSize(contentSize)
    {
    }

    bool Next() {
        do {
            Ptr += HASH_SIZE + ContentSize;
        } while (Ptr < End && Empty());

        return Ptr < End;
    }

    const char* Get() const {
        return Ptr + HASH_SIZE;
    }

private:
    bool Empty() const {
        ui64 hash;
        memcpy(&hash, Ptr, HASH_SIZE);
        return hash == 0;
    }

    const char* Ptr;
    const char* End;
    const ui64 ContentSize;
};

using TOpenHashMapIterPtr = ::TIntrusivePtr<TOpenHashMapIter>;

class TOpenHashMap: public TThrRefBase {
public:
    TOpenHashMap(const TString& path, ui64 size, ui64 contentSize);

    void SetItem(const TString& key, const void* data);

    bool GetItem(const TString& key, void* data) const;

    void DelItem(const TString& key);

    char* Ptr() const;

    void Flush();

    TOpenHashMapIterPtr Iter() const;

    const TString& Path;
    const ui64 Size;
    const ui64 ContentSize;

private:
    ui64 PairSize() const {
        return HASH_SIZE + ContentSize;
    }

    ui64 Capacity() const {
        return Size / PairSize();
    }

    ui64 Offset(ui64 hash) const {
        return (hash % Capacity()) * PairSize();
    }

    THolder<TFileMap> Map;
};

using TOpenHashMapPtr = ::TIntrusivePtr<TOpenHashMap>;

inline TOpenHashMapPtr CreateOpenHashMap(const TString& path, ui64 size, ui64 contentSize) {
    return new TOpenHashMap(path, size, contentSize);
}

template<class T>
inline i64 SumValues(TOpenHashMapPtr m) {
    Y_ENSURE(m->ContentSize >= sizeof(T));

    TOpenHashMapIterPtr iter = m->Iter();
    i64 sum = 0;
    T val = 0;
    while (iter->Next()) {
        memcpy(&val, iter->Get(), sizeof(T));
        sum += val;
    }
    return sum;
}
