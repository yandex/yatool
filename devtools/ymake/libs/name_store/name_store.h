#pragma once

#include <devtools/ymake/common/path_definitions.h>

#include <library/cpp/on_disk/multi_blob/multiblob_builder.h>
#include <library/cpp/threading/light_rw_lock/lightrwlock.h>

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/utility.h>

#include <library/cpp/containers/absl/flat_hash_map.h>

#include <optional>

class MD5;
class IOutputStream;
struct IMemoryPool;

class TNameStore {
public:
    // Also the on-disk width of a name ID. Zero is reserved.
    using TNameId = ui32;

    TNameStore();
    ~TNameStore();

    size_t Size() const {
        TLightReadGuard guard(Lock_);
        return SizeUnlocked();
    }

    TNameId Add(TStringBuf name);
    TNameId GetId(TStringBuf name) const;
    TNameId GetIdNx(TStringBuf name) const;

    bool Has(TStringBuf name) const;
    bool CheckId(TNameId id) const;

    template <typename View>
    View GetName(TNameId id) const {
        if (!CheckId(id)) {
            return {};
        }
        return { this, id };
    }

    template <typename View>
    bool GetName(TNameId id, View& view) const {
        if (!CheckId(id)) {
            return false;
        }
        view = { this, id };
        return true;
    }

    template <typename View>
    View GetStoredName(const TStringBuf& name) {
        TNameId id = Add(name);
        return { this, id };
    }

    template <typename View>
    bool GetStoredName(const TStringBuf& name, View& view) {
        TNameId id = Add(name);
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
    TStringBuf GetStringBufName(TNameId id) const;

    // Diagnostics: loading a frozen image must not populate the heap overlay.
    size_t OverlaySize() const {
        TLightReadGuard guard(Lock_);
        return Names_.size();
    }

private:
    // NameDataStore holds this same lock across name + metadata operations.
    template <class V, class View>
    friend class TNameDataStore;

    // Caller must hold Lock_: shared for reads, exclusive for mutations.
    TNameId AddUnlocked(TStringBuf name);
    bool CheckIdUnlocked(TNameId id) const;
    void SaveUnlocked(TMultiBlobBuilder& builder) const;
    void LoadUnlocked(TBlob& multi, std::optional<ui64> expectedCount = std::nullopt);
    void LoadSingleBlobUnlocked(TBlob& blob, std::optional<ui64> expectedCount = std::nullopt);
    void ClearUnlocked();
    size_t SizeUnlocked() const { return FrozenCount_ + Names_.size(); }
    TNameId FindUnlocked(ui64 hash) const;
    TStringBuf StringUnlocked(TNameId id) const;
    void SaveUnlocked(IOutputStream* out) const;

    // pray hashes never clashes
    using TNameToId = absl::flat_hash_map<ui64, TNameId, TIdentity>;
    using TNames = TVector<TStringBuf>;

    friend class TCmdView;
    friend class TFileView;
    friend class TFileConf;

private:
    mutable TLightRWLock Lock_;
    TAutoPtr<IMemoryPool> Pool_;

    TBlob Blob_;
    size_t FrozenCount_ = 0;
    size_t DirectoryOffset_ = 0;
    size_t DescriptorsOffset_ = 0;
    size_t IndexOffset_ = 0;
    size_t StringsOffset_ = 0;
    TNameToId Name2Id_;
    TNames Names_;
};
