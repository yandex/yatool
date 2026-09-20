#pragma once

#include <devtools/ymake/common/path_definitions.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>
#include <library/cpp/threading/light_rw_lock/lightrwlock.h>

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/utility.h>

#include <library/cpp/containers/absl/flat_hash_map.h>

class MD5;
class IOutputStream;
struct IMemoryPool;

class TNameStore {
public:
    TNameStore();
    ~TNameStore();

    size_t Size() const {
        TLightReadGuard guard(Lock_);
        return Names_.size();
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

    // Append keeps returned bytes stable. Clear/Load still require callers
    // to stop using all previously returned views.
    TStringBuf GetStringBufName(ui32 id) const;

private:
    // NameDataStore holds this same lock across name + metadata operations.
    template <class V, class View>
    friend class TNameDataStore;

    // Caller must hold Lock_: shared for reads, exclusive for mutations.
    ui32 AddUnlocked(TStringBuf name);
    bool CheckIdUnlocked(ui32 id) const;
    void SaveUnlocked(TMultiBlobBuilder& builder) const;
    void LoadUnlocked(TBlob& multi);
    void LoadSingleBlobUnlocked(TBlob& blob);
    void ClearUnlocked();

    // pray hashes never clashes
    using TNameToId = absl::flat_hash_map<ui64, ui32, TIdentity>;
    using TNames = TVector<TStringBuf>;

    friend class TCmdView;
    friend class TFileView;
    friend class TFileConf;

private:
    mutable TLightRWLock Lock_;
    TAutoPtr<IMemoryPool> Pool_;

    TBlob Blob_;
    TNameToId Name2Id_;
    TNames Names_;
};
