#pragma once

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/common/path_definitions.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/utility.h>

#include <library/cpp/containers/absl_flat_hash/flat_hash_map.h>

#include <compare>

class MD5;
class IOutputStream;

class TNameStore {
private:
    // pray hashes never clashes
    typedef absl::flat_hash_map<ui64, ui32, TIdentity> TNameToId;
    typedef TVector<TStringBuf> TNames;

    TAutoPtr<IMemoryPool> Pool;

    TBlob Blob;
    TNameToId Name2Id;
    TNames Names;

    friend class TCmdView;
    friend class TFileView;
    friend class TFileConf;
public:
    TNameStore() {
        // pray 64bit hash never be 0
        //Name2Id.set_empty_key(0);
        Clear();
    }

    ~TNameStore();

    size_t Size() const {
        return Names.size();
    }

    ui32 Add(TStringBuf name);
    ui32 GetId(TStringBuf name) const;
    ui32 GetIdNx(TStringBuf name) const;

    bool Has(TStringBuf name) const;
    bool CheckId(ui32 id) const;

    template <typename View>
    View GetName(ui32 id) const {
        if (!CheckId(id)) {
            return {};
        }
        return { this, id };
    }

    template <typename View>
    bool GetName(ui32 id, View& view) const {
        if (!CheckId(id)) {
            return false;
        }
        view = { this, id };
        return true;
    }

    template <typename View>
    View GetStoredName(const TStringBuf& name) {
        ui32 id = Add(name);
        return { this, id };
    }

    template <typename View>
    bool GetStoredName(const TStringBuf& name, View& view) {
        ui32 id = Add(name);
        view = { this, id };
        return true;
    }

    void Save(IOutputStream* out) const;
    void Save(TMultiBlobBuilder& builder) const;
    void Load(TBlob& multi);
    void LoadSingleBlob(TBlob& blob);

    void Clear();

private:
    TStringBuf GetStringBufName(ui32 id) const;
};
