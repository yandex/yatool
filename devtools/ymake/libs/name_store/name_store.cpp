#include "name_store.h"

#include <devtools/ymake/common/memory_pool.h>

#include <util/digest/city.h>
#include <util/generic/algorithm.h>
#include <util/generic/buffer.h>
#include <util/generic/xrange.h>
#include <util/stream/buffer.h>
#include <util/stream/length.h>
#include <util/stream/mem.h>
#include <util/stream/null.h>
#include <util/ysaveload.h>

#include <array>
#include <limits>

namespace {
    // Image: header, hash directory, ID-ordered descriptors, hash-ordered index,
    // then NUL-terminated strings. Descriptor offsets are relative to the strings.
    struct THeader {
        static constexpr ui64 ExpectedMagic = 0x594d4e414d455331ULL; // YMNAMES1
        static constexpr ui64 ExpectedVersion = 1;

        ui64 Magic = ExpectedMagic;
        ui64 Version = ExpectedVersion;
        ui64 NameCount = 0;
        ui64 IndexCount = 0;
        ui64 PayloadSize = 0;

        Y_SAVELOAD_DEFINE(Magic, Version, NameCount, IndexCount, PayloadSize);
    };

    struct TNameDescriptor {
        ui64 Offset = 0;
        ui32 Length = 0;

        Y_SAVELOAD_DEFINE(Offset, Length);
    };

    struct THashIndexEntry {
        ui64 Hash = 0;
        TNameStore::TNameId Id = 0;

        Y_SAVELOAD_DEFINE(Hash, Id);
    };

    struct THashDirectory {
        static constexpr size_t PrefixBits = 8;
        static constexpr size_t BucketCount = size_t{1} << PrefixBits;
        using TBoundaries = std::array<ui64, BucketCount + 1>;

        static size_t Prefix(ui64 hash) noexcept {
            return hash >> (std::numeric_limits<ui64>::digits - PrefixBits);
        }
    };

    ui64 GoodHash(TStringBuf s) noexcept {
        return CityHash64(s.data(), s.size());
    }

    template <class T>
    size_t SerializedSize(const T& value) {
        TNullOutput nullOutput;
        TCountingOutput output(&nullOutput);
        ::Save(&output, value);
        return output.Counter();
    }

    // These records have fixed-width serialization. Measure each size only once.
    template <class T>
    size_t RecordSize() {
        static const size_t size = SerializedSize(T{});
        return size;
    }

    // LoadSingleBlobUnlocked checks the complete section bounds before attaching the image.
    template <class T>
    inline T LoadIndexRecord(const TBlob& blob, size_t offset, size_t size) {
        TMemoryInput input(blob.AsCharPtr() + offset, size);
        T value;
        ::Load(&input, value);
        Y_ASSERT(input.Exhausted());
        return value;
    }
}

TNameStore::TNameStore() {
    Clear();
}

TNameStore::~TNameStore() = default;

TNameStore::TNameId TNameStore::FindUnlocked(ui64 hash) const {
    if (const auto it = Name2Id_.find(hash); it != Name2Id_.end()) {
        return it->second;
    }
    if (!FrozenCount_) {
        return 0;
    }
    const size_t prefix = THashDirectory::Prefix(hash);
    ui64 begin, end;
    TMemoryInput input(Blob_.AsCharPtr() + DirectoryOffset_ + prefix * sizeof(ui64), sizeof(begin) + sizeof(end));
    ::LoadMany(&input, begin, end);
    Y_ENSURE(begin <= end && end < FrozenCount_, "Invalid names hash directory");
    const auto indexes = xrange(begin, end);
    const size_t entrySize = RecordSize<THashIndexEntry>();
    const auto entryAt = [this, entrySize](ui64 index) {
        return LoadIndexRecord<THashIndexEntry>(Blob_, IndexOffset_ + index * entrySize, entrySize);
    };
    const auto it = LowerBoundBy(indexes.begin(), indexes.end(), hash, [&](ui64 index) {
        return entryAt(index).Hash;
    });
    if (it != indexes.end()) {
        const auto entry = entryAt(*it);
        if (entry.Hash == hash) {
            Y_ENSURE(entry.Id && entry.Id < FrozenCount_, "Invalid names hash index ID");
            return entry.Id;
        }
    }
    return 0;
}

TNameStore::TNameId TNameStore::Add(TStringBuf name) {
    TLightWriteGuard guard(Lock_);
    return AddUnlocked(name);
}

TNameStore::TNameId TNameStore::AddUnlocked(TStringBuf name) {
    const auto hash = GoodHash(name);
    if (const TNameId id = FindUnlocked(hash)) {
        return id;
    }
    Y_ENSURE(SizeUnlocked() < std::numeric_limits<TNameId>::max(), "Too many names");
    const TNameId id = SizeUnlocked();
    const auto stored = Pool_->Append(name);
    Names_.push_back(stored);
    Name2Id_[hash] = id;
    return id;
}

TNameStore::TNameId TNameStore::GetId(TStringBuf name) const {
    if (const auto id = GetIdNx(name)) {
        return id;
    }
    throw yexception() << "No id available for '" << name << "'\n";
}

TNameStore::TNameId TNameStore::GetIdNx(TStringBuf name) const {
    TLightReadGuard guard(Lock_);
    return FindUnlocked(GoodHash(name));
}

bool TNameStore::Has(TStringBuf name) const {
    return GetIdNx(name) != 0;
}

bool TNameStore::CheckId(TNameId id) const {
    TLightReadGuard guard(Lock_);
    return CheckIdUnlocked(id);
}

bool TNameStore::CheckIdUnlocked(TNameId id) const {
    Y_ENSURE(id, "GetName: internal error: trying to get name for id = 0");
    return id < SizeUnlocked();
}

void TNameStore::Clear() {
    TLightWriteGuard guard(Lock_);
    ClearUnlocked();
}

void TNameStore::ClearUnlocked() {
    IMemoryPool::Construct().Swap(Pool_);
    TNameToId().swap(Name2Id_);
    TNames().swap(Names_);
    Names_.push_back(Pool_->Append(TStringBuf()));
    Blob_ = {};
    FrozenCount_ = DirectoryOffset_ = DescriptorsOffset_ = IndexOffset_ = StringsOffset_ = 0;
}

TStringBuf TNameStore::StringUnlocked(TNameId id) const {
    if (id >= FrozenCount_) {
        return Names_[id - FrozenCount_];
    }
    const size_t descriptorSize = RecordSize<TNameDescriptor>();
    const auto [offset, length] = LoadIndexRecord<TNameDescriptor>(Blob_, DescriptorsOffset_ + id * descriptorSize, descriptorSize);
    const size_t size = Blob_.Size() - StringsOffset_;
    Y_ENSURE(offset < size && length < size - offset, "Invalid names string range");
    const char* data = Blob_.AsCharPtr() + StringsOffset_ + offset;
    Y_ENSURE(data[length] == 0, "Invalid names string terminator");
    return {data, length};
}

void TNameStore::SaveUnlocked(IOutputStream* out) const {
    TVector<THashIndexEntry> added;
    added.reserve(Name2Id_.size());
    for (const auto& [hash, id] : Name2Id_) {
        added.push_back({hash, id});
    }
    SortBy(added, [](const auto& entry) { return entry.Hash; });

    THashDirectory::TBoundaries directory{};
    const size_t entrySize = RecordSize<THashIndexEntry>();
    // Frozen hashes need not touch any string pages, even while saving.
    for (size_t i = 0; i + 1 < FrozenCount_; ++i) {
        const auto entry = LoadIndexRecord<THashIndexEntry>(Blob_, IndexOffset_ + i * entrySize, entrySize);
        ++directory[THashDirectory::Prefix(entry.Hash) + 1];
    }
    for (const auto& entry : added) {
        ++directory[THashDirectory::Prefix(entry.Hash) + 1];
    }
    for (size_t i = 1; i < directory.size(); ++i) {
        directory[i] += directory[i - 1];
    }
    ui64 payloadSize = FrozenCount_ ? Blob_.Size() - StringsOffset_ : 0;
    for (const auto name : Names_) {
        Y_ENSURE(name.size() <= std::numeric_limits<ui32>::max(), "Name is too long");
        payloadSize += name.size() + 1;
    }
    ::Save(out, THeader{.NameCount = SizeUnlocked(), .IndexCount = directory.back(), .PayloadSize = payloadSize});
    ::SavePodArray(out, directory.data(), directory.size());

    ui64 offset = 0;
    if (FrozenCount_) {
        out->Write(Blob_.AsCharPtr() + DescriptorsOffset_, IndexOffset_ - DescriptorsOffset_);
        offset = Blob_.Size() - StringsOffset_;
    }
    for (const auto name : Names_) {
        ::Save(out, TNameDescriptor{offset, static_cast<ui32>(name.size())});
        offset += name.size() + 1;
    }

    size_t old = 0;
    size_t fresh = 0;
    while (old + 1 < FrozenCount_ || fresh < added.size()) {
        const size_t entryOffset = IndexOffset_ + old * entrySize;
        if (old + 1 < FrozenCount_ && (fresh == added.size() || LoadIndexRecord<THashIndexEntry>(Blob_, entryOffset, entrySize).Hash < added[fresh].Hash)) {
            out->Write(Blob_.AsCharPtr() + entryOffset, entrySize);
            ++old;
        } else {
            ::Save(out, added[fresh++]);
        }
    }
    if (FrozenCount_) {
        out->Write(Blob_.AsCharPtr() + StringsOffset_, Blob_.Size() - StringsOffset_);
    }
    for (const auto name : Names_) {
        out->Write(name.data(), name.size() + 1);
    }
}

void TNameStore::Save(TMultiBlobBuilder& builder) const {
    TLightReadGuard guard(Lock_);
    SaveUnlocked(builder);
}

void TNameStore::SaveUnlocked(TMultiBlobBuilder& builder) const {
    TBuffer buffer;
    TBufferOutput output(buffer);
    SaveUnlocked(&output);
    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBuffer(buffer)));
}

void TNameStore::Save(IOutputStream* out) const {
    TLightReadGuard guard(Lock_);
    SaveUnlocked(out);
}

void TNameStore::Load(TBlob& multi) {
    TLightWriteGuard guard(Lock_);
    LoadUnlocked(multi);
}

void TNameStore::LoadUnlocked(TBlob& multi, std::optional<ui64> expectedCount) {
    TSubBlobs blobs(multi);
    Y_ENSURE(blobs.size() == 1, "Invalid names image container");
    LoadSingleBlobUnlocked(blobs[0], expectedCount);
}

void TNameStore::LoadSingleBlob(TBlob& blob) {
    TLightWriteGuard guard(Lock_);
    LoadSingleBlobUnlocked(blob);
}

void TNameStore::LoadSingleBlobUnlocked(TBlob& blob, std::optional<ui64> expectedCount) {
    TMemoryInput input(blob.Data(), blob.Size());
    THeader header;
    ::Load(&input, header);
    Y_ENSURE(header.Magic == THeader::ExpectedMagic && header.Version == THeader::ExpectedVersion, "Unsupported names image format");
    Y_ENSURE(header.NameCount && header.NameCount <= std::numeric_limits<TNameId>::max() && header.IndexCount == header.NameCount - 1,
             "Invalid names image count");
    Y_ENSURE(!expectedCount || header.NameCount == *expectedCount, "Names and metadata counts differ");
    const size_t directoryOffset = blob.Size() - input.Avail();
    THashDirectory::TBoundaries directory;
    ::LoadPodArray(&input, directory.data(), directory.size());
    Y_ENSURE(directory.front() == 0 && directory.back() == header.IndexCount && std::is_sorted(directory.begin(), directory.end()),
             "Invalid names image directory");
    size_t offset = blob.Size() - input.Avail();
    const auto consume = [&](ui64 items, size_t size) {
        Y_ENSURE(offset <= blob.Size() && items <= (blob.Size() - offset) / size, "Truncated names image section");
        offset += items * size;
    };
    const size_t descriptors = offset;
    consume(header.NameCount, RecordSize<TNameDescriptor>());
    const size_t index = offset;
    consume(header.IndexCount, RecordSize<THashIndexEntry>());
    Y_ENSURE(header.PayloadSize == blob.Size() - offset, "Invalid names image payload size");

    // No per-name allocations, hashing, or payload scan here.
    ClearUnlocked();
    Names_.clear();
    Blob_ = blob;
    FrozenCount_ = header.NameCount;
    DirectoryOffset_ = directoryOffset;
    DescriptorsOffset_ = descriptors;
    IndexOffset_ = index;
    StringsOffset_ = offset;
}

TStringBuf TNameStore::GetStringBufName(TNameId id) const {
    TLightReadGuard guard(Lock_);
    return CheckIdUnlocked(id) ? StringUnlocked(id) : TStringBuf{};
}
