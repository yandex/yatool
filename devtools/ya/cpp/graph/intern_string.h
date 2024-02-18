#pragma once

#include "devtools/ya/cpp/lib/edl/common/loaders.h"

#include <typeindex>
#include <util/generic/hash.h>
#include <util/generic/singleton.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/memory/pool.h>
#include <util/system/spinlock.h>

#include <array>

namespace NYa::NGraph {
    // Hash with open addressing.
    // Algorithm D from Knuth Vol. 3, Sec. 6.4.
    // Idea of internal stuctures is got from Python 3 dict implementation (contrib/tools/python3/Objects/dictobject.c)
    class TInternStringStoragePartition {
    public:
        using TIndex = i32;

        TInternStringStoragePartition(size_t initCapacity = MinCapacity) {
            size_t capacity = AdjustAndCheckCapacity(initCapacity);
            Init(capacity);
        }

        inline TIndex GetIndex(TStringBuf value) {
            size_t valueHash = THash<TStringBuf>{}(value);
            size_t i = Lookup(value, valueHash);
            if (TIndex idx = Indices_[i]; idx != Unused) {
                return idx;
            }
            if (NeedExtend()) {
                Extend();
                i = FindFreeSlot(Indices_, valueHash);
            }
            TIndex idx = Entries_.size();
            const char* data = Pool_.Append(value.data(), value.length());
            Entries_.push_back({.Value = TStringBuf{data, value.length()}, .Hash = valueHash});
            AllocatedBytes_ += value.length();
            Indices_[i] = idx;
            return idx;
        }

        inline TStringBuf GetValue(TIndex index) const {
            return Entries_.at(index).Value;
        }

        inline size_t AllocatedBytes() const noexcept {
            return AllocatedBytes_;
        }

        inline size_t Size() const noexcept {
            return Entries_.size();
        }

        inline size_t Capacity() const noexcept {
            return Indices_.size();
        }

        inline void Clear() {
            Init(MinCapacity);
        }

    private:
        static constexpr size_t MinCapacity = 1024;
        static constexpr size_t MaxCapacity = (size_t)Max<TIndex>() + 1;
        static constexpr TIndex Unused = -1;

        struct TEntryItem {
            TStringBuf Value;
            size_t Hash;
        };

        using TEntries = TVector<TEntryItem>;
        using TIndices = TVector<TIndex>;

    private:
        inline void Init(size_t capacity) {
            Entries_.clear();
            Pool_.Clear();
            AllocatedBytes_ = 0;
            Indices_.clear();
            Indices_.resize(capacity, Unused);
        }

        inline size_t AdjustAndCheckCapacity(size_t capacity) {
            Y_ENSURE(capacity <= MaxCapacity);
            if (capacity < MinCapacity) {
                return MinCapacity;
            }
            if ((capacity - 1) & capacity) {
                // Next power of 2: http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
                --capacity;
                capacity |= capacity >> 1;
                capacity |= capacity >> 2;
                capacity |= capacity >> 4;
                capacity |= capacity >> 8;
                capacity |= capacity >> 16;
                capacity |= capacity >> 32;
                ++capacity;
            }
            return capacity;
        }

        inline void Extend() {
            size_t capacity = Indices_.size() * 2;
            Y_ENSURE(capacity <= MaxCapacity, "Container capacity overflow");

            TIndices newIndices(capacity, Unused);
            for (size_t idx = 0; idx < Entries_.size(); ++idx) {
                size_t i = FindFreeSlot(newIndices, Entries_[idx].Hash);
                newIndices[i] = idx;
            }

            Indices_ = std::move(newIndices);
        }

        inline size_t FindIfUnusedOr(TIndices& indices, size_t valueHash, auto&& predicate) {
            size_t capacity = indices.size();
            size_t i = valueHash % capacity;
            size_t c = ((valueHash) << 2 | 1) % capacity;
            for(;;) {
                TIndex idx = indices[i];
                if (idx == Unused || predicate(idx)) {
                    return i;
                } else {
                    i = (i - c) % capacity;
                }
            }
        }

        inline size_t Lookup(TStringBuf value, size_t valueHash) {
            auto pred = [&value, &valueHash, this](TIndex& idx) -> bool {
                Y_ASSERT(size_t(idx) < Entries_.size());
                TEntryItem& item = Entries_[idx];
                return item.Hash == valueHash && item.Value == value;
            };
            return FindIfUnusedOr(Indices_, valueHash, pred);
        }

        inline size_t FindFreeSlot(TIndices& indices, size_t valueHash) {
            return FindIfUnusedOr(indices, valueHash, [](auto&&) {return false;});
        }

        inline bool NeedExtend() {
            return Entries_.size() >= Indices_.size() * 2 / 3;
        }

    private:
        size_t AllocatedBytes_{0};
        TEntries Entries_{};
        TIndices Indices_{};
        TMemoryPool Pool_{1 << 20};
    };

    class TInternStringStorage : TNonCopyable {
    public:
        using TIndex = TInternStringStoragePartition::TIndex;
        static constexpr TStringBuf EmptyString{};
        static constexpr TIndex EmptyStringIndex = 0;

        TInternStringStorage() {
            InitEmptyString();
        }

        inline TIndex GetIndex(TStringBuf value) {
            if (!value) {
                return EmptyStringIndex;
            }
            TIndex partitionNum = value.length() % (1 << PartitionBits);
            TPartition& partition = Partitions_[partitionNum];
            TGuard<TAdaptiveLock> guard(partition.Lock);
            TIndex index = partition.Storage.GetIndex(value);
            Y_ENSURE(index < (Max<TIndex>() >> PartitionBits), "Storage overflows");
            return (index << PartitionBits) + partitionNum;
        }

        inline TStringBuf GetValue(TIndex index) {
            if (index == EmptyStringIndex) {
                return EmptyString;
            }
            TIndex partitionNum = index % (1 << PartitionBits);
            TPartition& partition = Partitions_[partitionNum];
            TGuard<TAdaptiveLock> guard(partition.Lock);
            return partition.Storage.GetValue(index >> PartitionBits);
        }

        /* Important:
            - after Clear() calling all TInternString objects become invalid.
            - Clear() is not multithreaded.
        */
        inline void Clear() {
            for (TPartition& partition : Partitions_) {
                partition.Storage.Clear();
            }
            InitEmptyString();
        }

        inline size_t AllocatedBytes() const {
            return Aggregate(&TInternStringStoragePartition::AllocatedBytes);
        }

        inline size_t Size() const {
            return Aggregate(&TInternStringStoragePartition::Size);
        }

        inline size_t Capacity() const {
            return Aggregate(&TInternStringStoragePartition::Capacity);
        }

        inline static TInternStringStorage& Instance() {
            return *Singleton<TInternStringStorage>();
        }

    private:
        static constexpr ui32 PartitionBits = 4;

        struct TPartition {
            mutable TAdaptiveLock Lock{};
            TInternStringStoragePartition Storage{};
        };

    private:
        void InitEmptyString() {
            TIndex emptyIndex = Partitions_[0].Storage.GetIndex(EmptyString);
            Y_ASSERT(emptyIndex == EmptyStringIndex);
        }

        size_t Aggregate(size_t(TInternStringStoragePartition::*getter)() const) const {
            size_t result = 0;
            for (auto&& p : Partitions_) {
                result += (p.Storage.*getter)();
            }
            return result;
        }

    private:
        std::array<TPartition, 1 << PartitionBits> Partitions_{};
    };

    class TInternString {
    public:
        TInternString() noexcept = default;
        TInternString(TStringBuf value) {
            Index_ = TInternStringStorage::Instance().GetIndex(value);
        }

        TInternString(const TInternString& other) noexcept = default;
        TInternString(const TInternString&& other) noexcept = default;

        inline operator TStringBuf() const {
            return Get();
        }

        inline explicit operator bool() const noexcept {
            return Index_ != TInternStringStorage::EmptyStringIndex;
        }

        inline TStringBuf Get() const {
            return TInternStringStorage::Instance().GetValue(Index_);
        }

        inline TInternString& operator=(const TInternString& other) noexcept = default;
        inline TInternString& operator=(TInternString&& other) noexcept = default;

        inline TInternString& operator=(TStringBuf value) {
            Index_ = TInternStringStorage::Instance().GetIndex(value);
            return *this;
        }

        inline bool operator==(const TInternString& other) const noexcept {
            return other.Index_ == Index_;
        }

        inline bool operator<(const TInternString& other) const {
            return Get() < other.Get();
        }

        friend THash<TInternString>;
    private:
        TInternStringStorage::TIndex Index_{TInternStringStorage::EmptyStringIndex};
    };
}

template <>
struct THash<NYa::NGraph::TInternString> {
public:
    size_t operator()(const NYa::NGraph::TInternString& v) const noexcept {
        return v.Index_;
    }
};

template <>
class NYa::NEdl::TLoader<NYa::NGraph::TInternString> : public NYa::NEdl::TStringValueLoader<NYa::NGraph::TInternString> {
public:
    using NYa::NEdl::TStringValueLoader<NGraph::TInternString>::TStringValueLoader;
};

namespace NPrivate {
    template <>
    inline TString MapKeyToString(const NYa::NGraph::TInternString& key) {
        return TString{key.Get()};
    }
}
