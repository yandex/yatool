#pragma once

#include "name_store.h"

#include <util/ysaveload.h>
#include <util/generic/deque.h>
#include <util/generic/buffer.h>
#include <util/stream/buffer.h>

template <class V, class View>
class TNameDataStore {
public:
    using TValue = V;

    void PutById(ui32 id, const V& data) {
        Y_ASSERT(id < Meta_.size());
        Meta_[id] = data;
    }

    const V& GetById(ui32 id) const {
        Y_ASSERT(id < Meta_.size());
        return Meta_[id];
    }

    V& GetById(ui32 id) {
        Y_ASSERT(id < Meta_.size());
        return Meta_[id];
    }

    size_t Size() const {
        return NameStore_.Size();
    }

    View GetName(ui32 id) const {
        return NameStore_.GetName<View>(id);
    }

    View GetStoredName(const TStringBuf& name) {
        return NameStore_.GetStoredName<View>(name);
    }

    ui32 Add(TStringBuf name) {
        ui32 newId = NameStore_.Add(name);
        if (Meta_.size() <= newId)
            Meta_.resize(newId + 1);
        return newId;
    }

    bool HasName(TStringBuf name) const {
        return NameStore_.Has(name);
    }

    ui32 GetId(TStringBuf name) const {
        return NameStore_.GetId(name);
    }

    ui32 GetIdNx(TStringBuf name) const {
        return NameStore_.GetIdNx(name);
    }

    void Save(TMultiBlobBuilder& builder) {
        TMultiBlobBuilder* multi = new TMultiBlobBuilder();
        NameStore_.Save(*multi);
        builder.AddBlob(multi);

        if (Meta_.size() > 0) {
            TBuffer buffer;
            TBufferOutput output(buffer);
            TSerializer<decltype(Meta_)>::Save(&output, Meta_);
            builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
        }
    }

    void Load(TBlob& multi) {
        Clear();
        TSubBlobs blob(multi);

        NameStore_.Load(blob[0]);

        if (blob.size() == 2) {
            TMemoryInput input(blob[1].Data(), blob[1].Length());
            TSerializer<decltype(Meta_)>::Load(&input, Meta_);
        }
    }

    void Clear() {
        NameStore_.Clear();
        Meta_.clear();
    }

    void Dump(IOutputStream& out) const {
        for (size_t n = 1; n < Meta_.size(); n++) {
            if (!NameStore_.CheckId(n))
                continue;
            out << n << "\t" << NameStore_.GetName<View>(n) << "\t" << Meta_[n] << Endl;
        }
    }

protected:
    using TData = TDeque<V>;
    friend class TTimeStamps;

    TNameStore NameStore_;
    TData Meta_;
};
