#pragma once

#include "name_store.h"

#include <util/ysaveload.h>
#include <util/generic/deque.h>
#include <util/generic/buffer.h>
#include <util/stream/buffer.h>

template <class V, class View>
class TNameDataStore {
protected:
    TNameStore NameStore;
    typedef TDeque<V> TData;
    TData Meta;
    friend class TTimeStamps;

public:
    using TValue = V;

    void PutById(ui32 id, const V& data) {
        Y_ASSERT(id < Meta.size());
        Meta[id] = data;
    }

    const V& GetById(ui32 id) const {
        Y_ASSERT(id < Meta.size());
        return Meta[id];
    }

    V& GetById(ui32 id) {
        Y_ASSERT(id < Meta.size());
        return Meta[id];
    }

    size_t Size() const {
        return NameStore.Size();
    }

    View GetName(ui32 id) const {
        return NameStore.GetName<View>(id);
    }

    View GetStoredName(const TStringBuf& name) {
        return NameStore.GetStoredName<View>(name);
    }

    ui32 Add(TStringBuf name) {
        ui32 newId = NameStore.Add(name);
        if (Meta.size() <= newId)
            Meta.resize(newId + 1);
        return newId;
    }

    bool HasName(TStringBuf name) const {
        return NameStore.Has(name);
    }

    ui32 GetId(TStringBuf name) const {
        return NameStore.GetId(name);
    }

    ui32 GetIdNx(TStringBuf name) const {
        return NameStore.GetIdNx(name);
    }

    void Save(TMultiBlobBuilder& builder) {
        TMultiBlobBuilder* multi = new TMultiBlobBuilder();
        NameStore.Save(*multi);
        builder.AddBlob(multi);

        if (Meta.size() > 0) {
            TBuffer buffer;
            TBufferOutput output(buffer);
            TSerializer<decltype(Meta)>::Save(&output, Meta);
            builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
        }
    }

    void Load(TBlob& multi) {
        Clear();
        TSubBlobs blob(multi);

        NameStore.Load(blob[0]);

        if (blob.size() == 2) {
            TMemoryInput input(blob[1].Data(), blob[1].Length());
            TSerializer<decltype(Meta)>::Load(&input, Meta);
        }
    }

    void Clear() {
        NameStore.Clear();
        Meta.clear();
    }

    void Dump(IOutputStream& out) const {
        for (size_t n = 1; n < Meta.size(); n++) {
            if (!NameStore.CheckId(n))
                continue;
            out << n << "\t" << NameStore.GetName<View>(n) << "\t" << Meta[n] << Endl;
        }
    }
};
