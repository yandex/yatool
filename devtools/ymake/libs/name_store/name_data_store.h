#pragma once

#include "name_store.h"

#include <util/ysaveload.h>
#include <util/generic/buffer.h>
#include <util/stream/buffer.h>

#include <array>
#include <unordered_map>
#include <utility>

struct TMetadataStampRemap {
    std::array<ui8, 256> Checked;
    std::array<ui8, 256> Modified;

    TMetadataStampRemap() {
        for (size_t i = 0; i < 256; ++i) {
            Checked[i] = Modified[i] = i;
        }
    }
};

// Most name data has no session stamps. File/command data specialize this.
template <class V>
struct TNameDataTraits {
    static constexpr size_t RecordSize = 0; // variable-size serialization
    static ui8 Stamp(const V&) { return 0; }
    static void Remap(V&, TNameStore::TNameId, const TMetadataStampRemap&) {}
};

template <class V, class View>
class TNameDataStore {
public:
    using TValue = V;
    using TNameId = TNameStore::TNameId;
    using THistogram = std::array<ui64, 256>;

    void PutById(TNameId id, const V& data) {
        TLightWriteGuard guard(NameStore_.Lock_);
        Y_ENSURE(id < NameStore_.SizeUnlocked(), "Invalid metadata ID");
        Meta_[id] = data;
    }

    // References survive insertion and rehash. As before, callers synchronize
    // mutations through returned references and stop using them at Clear/Load.
    const V& GetById(TNameId id) const {
        {
            TLightReadGuard guard(NameStore_.Lock_);
            if (const auto it = Meta_.find(id); it != Meta_.end()) {
                return it->second;
            }
        }
        TLightWriteGuard guard(NameStore_.Lock_);
        return MaterializeUnlocked(id);
    }

    V& GetById(TNameId id) {
        return const_cast<V&>(std::as_const(*this).GetById(id));
    }

    size_t MaterializedSize() const {
        TLightReadGuard guard(NameStore_.Lock_);
        return Meta_.size();
    }

    size_t Size() const { return NameStore_.Size(); }
    View GetName(TNameId id) const { return NameStore_.GetName<View>(id); }
    View GetStoredName(const TStringBuf& name) {
        return GetName(Add(name));
    }

    TNameId Add(TStringBuf name) {
        TLightWriteGuard guard(NameStore_.Lock_);
        // New metadata is implicit default data until first access. In
        // particular, do not resize a container to the frozen ID prefix.
        return NameStore_.AddUnlocked(name);
    }

    bool HasName(TStringBuf name) const { return NameStore_.Has(name); }
    TNameId GetId(TStringBuf name) const { return NameStore_.GetId(name); }
    TNameId GetIdNx(TStringBuf name) const { return NameStore_.GetIdNx(name); }
    const TNameStore& GetNameStore() const noexcept { return NameStore_; }

    THistogram StampHistogram() const {
        TLightReadGuard guard(NameStore_.Lock_);
        return HistogramUnlocked();
    }

    // Remap frozen records logically, without visiting their pages. Objects
    // already handed to callers are updated in place, preserving references.
    void RemapStamps(const TMetadataStampRemap& remap) {
        TLightWriteGuard guard(NameStore_.Lock_);
        // Only the frozen prefix can keep deferred transformations: defaults
        // of names inserted after this call belong to the new stamp space.
        for (size_t id = FrozenCount_; id < NameStore_.SizeUnlocked(); ++id) {
            MaterializeUnlocked(id);
        }
        THistogram histogram{};
        for (size_t i = 0; i < 256; ++i) {
            histogram[remap.Modified[i]] += FrozenHistogram_[i];
        }
        FrozenHistogram_ = histogram;
        for (auto& [id, value] : Meta_) {
            TNameDataTraits<V>::Remap(value, id, remap);
        }
        for (size_t i = 0; i < 256; ++i) {
            Remap_.Checked[i] = remap.Checked[Remap_.Checked[i]];
            Remap_.Modified[i] = remap.Modified[Remap_.Modified[i]];
        }
    }

    void Save(TMultiBlobBuilder& builder) {
        TLightReadGuard guard(NameStore_.Lock_);
        auto multi = MakeHolder<TMultiBlobBuilder>();
        NameStore_.SaveUnlocked(*multi);
        builder.AddBlob(multi.Release());

        TBuffer buffer;
        TBufferOutput output(buffer);
        const ui64 count = NameStore_.SizeUnlocked();
        const ui64 recordSize = TNameDataTraits<V>::RecordSize;
        ::SaveMany(&output, Magic, Version, count, recordSize);
        const auto histogram = HistogramUnlocked();
        ::SavePodArray(&output, histogram.data(), histogram.size());

        // Fixed-size file/command records need no per-ID offset table.
        if (recordSize) {
            for (ui64 id = 0; id < count; ++id) {
                const auto it = Meta_.find(id);
                const V value = it != Meta_.end() ? it->second : ReadFrozenUnlocked(id);
                const size_t before = buffer.Size();
                ::Save(&output, value);
                Y_ENSURE(buffer.Size() - before == recordSize, "Unexpected metadata record size");
            }
            builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
            return;
        }

        // An offsets table also supports variable-sized V.
        // Serialization uses temporary values, never populating the overlay.
        TBuffer payload;
        TBufferOutput records(payload);
        for (ui64 id = 0; id < count; ++id) {
            ::Save(&output, static_cast<ui64>(payload.Size()));
            const auto it = Meta_.find(id);
            const V value = it != Meta_.end() ? it->second : ReadFrozenUnlocked(id);
            ::Save(&records, value);
        }
        ::Save(&output, static_cast<ui64>(payload.Size()));
        output.Write(payload.Data(), payload.Size());
        builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
    }

    void Load(TBlob& multi) {
        TLightWriteGuard guard(NameStore_.Lock_);
        TSubBlobs blobs(multi);
        Y_ENSURE(blobs.size() == 2, "Invalid name data image container");
        TMemoryInput input(blobs[1].Data(), blobs[1].Size());
        ui64 magic, version, count, recordSize;
        ::LoadMany(&input, magic, version, count, recordSize);
        Y_ENSURE(magic == Magic && version == Version, "Unsupported name data image format");
        Y_ENSURE(recordSize == TNameDataTraits<V>::RecordSize, "Unsupported metadata record size");
        THistogram histogram;
        ::LoadPodArray(&input, histogram.data(), histogram.size());
        ui64 histogramCount = 0;
        for (auto frequency : histogram) {
            Y_ENSURE(frequency <= count - histogramCount, "Invalid metadata stamp histogram");
            histogramCount += frequency;
        }
        Y_ENSURE(histogramCount == count, "Incomplete metadata stamp histogram");
        const size_t offsets = blobs[1].Size() - input.Avail();
        if (!recordSize) {
            Y_ENSURE(input.Avail() >= sizeof(ui64) && count <= (input.Avail() - sizeof(ui64)) / sizeof(ui64), "Truncated metadata offsets");
        } else {
            Y_ENSURE(input.Avail() % recordSize == 0 && count == input.Avail() / recordSize, "Invalid metadata records size");
        }
        const size_t payload = recordSize ? offsets : offsets + (count + 1) * sizeof(ui64);
        const auto readOffset = [&](ui64 id) {
            ui64 result;
            memcpy(&result, blobs[1].AsCharPtr() + offsets + id * sizeof(ui64), sizeof(result));
            return result;
        };
        if (!recordSize) {
            Y_ENSURE(readOffset(0) == 0 && readOffset(count) == blobs[1].Size() - payload, "Invalid metadata payload size");
        }
        NameStore_.LoadUnlocked(blobs[0], count);
        ResetMetadataUnlocked();
        Image_ = blobs[1];
        FrozenCount_ = count;
        OffsetsOffset_ = offsets;
        PayloadOffset_ = payload;
        FrozenHistogram_ = histogram;
    }

    void Clear() {
        TLightWriteGuard guard(NameStore_.Lock_);
        NameStore_.ClearUnlocked();
        ResetMetadataUnlocked();
    }

    void Dump(IOutputStream& out) const {
        for (size_t id = 1; id < Size(); ++id) {
            out << id << "\t" << GetName(id) << "\t" << GetById(id) << Endl;
        }
    }

protected:
    TNameStore NameStore_;

private:
    static constexpr ui64 Magic = 0x594d4d4554413031ULL; // YMMETA01
    static constexpr ui64 Version = 1;

    V ReadFrozenUnlocked(TNameId id) const {
        V value{};
        if (id < FrozenCount_) {
            ui64 begin, end;
            if constexpr (TNameDataTraits<V>::RecordSize) {
                begin = static_cast<ui64>(id) * TNameDataTraits<V>::RecordSize;
                end = begin + TNameDataTraits<V>::RecordSize;
            } else {
                memcpy(&begin, Image_.AsCharPtr() + OffsetsOffset_ + id * sizeof(ui64), sizeof(begin));
                memcpy(&end, Image_.AsCharPtr() + OffsetsOffset_ + (static_cast<size_t>(id) + 1) * sizeof(ui64), sizeof(end));
            }
            Y_ENSURE(begin <= end && end <= Image_.Size() - PayloadOffset_, "Invalid metadata record range");
            TMemoryInput input(Image_.AsCharPtr() + PayloadOffset_ + begin, end - begin);
            ::Load(&input, value);
            Y_ENSURE(input.Exhausted(), "Trailing metadata record data");
            TNameDataTraits<V>::Remap(value, id, Remap_);
        }
        return value;
    }

    V& MaterializeUnlocked(TNameId id) const {
        Y_ENSURE(id < NameStore_.SizeUnlocked(), "Invalid metadata ID");
        auto it = Meta_.find(id);
        if (it == Meta_.end()) {
            it = Meta_.emplace(id, ReadFrozenUnlocked(id)).first;
        }
        return it->second;
    }

    THistogram HistogramUnlocked() const {
        auto result = FrozenHistogram_;
        result[TNameDataTraits<V>::Stamp(V{})] += NameStore_.SizeUnlocked() - FrozenCount_;
        for (const auto& [id, value] : Meta_) {
            auto& frequency = result[TNameDataTraits<V>::Stamp(ReadFrozenUnlocked(id))];
            Y_ENSURE(frequency, "Metadata stamp histogram differs from records");
            --frequency;
            ++result[TNameDataTraits<V>::Stamp(value)];
        }
        return result;
    }

    void ResetMetadataUnlocked() {
        decltype(Meta_)().swap(Meta_);
        Image_ = {};
        FrozenCount_ = OffsetsOffset_ = PayloadOffset_ = 0;
        FrozenHistogram_.fill(0);
        Remap_ = {};
    }

    mutable std::unordered_map<TNameId, V> Meta_;
    TBlob Image_;
    size_t FrozenCount_ = 0;
    size_t OffsetsOffset_ = 0;
    size_t PayloadOffset_ = 0;
    THistogram FrozenHistogram_{};
    TMetadataStampRemap Remap_;
};
