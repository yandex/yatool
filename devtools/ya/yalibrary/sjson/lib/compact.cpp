#include "compact.h"

#include <util/generic/vector.h>
#include <util/generic/yexception.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace NSJson {
    namespace {
        constexpr size_t CacheSize = 8192;

        // Graph records usually have fewer than sixteen fields/elements. Keep
        // the builder's small frames on the stack, not in millions of mallocs.
        class TInlineValues {
        public:
            size_t size() const { return Size_; }
            bool empty() const { return !Size_; }
            const TIndexType* begin() const { return Size_ <= 16 ? Small_ : Large_.data(); }
            const TIndexType* end() const { return begin() + Size_; }
            TIndexType& operator[](size_t i) { return Size_ <= 16 ? Small_[i] : Large_[i]; }
            const TIndexType& operator[](size_t i) const { return begin()[i]; }
            void push_back(TIndexType value) {
                if (Size_ < 16) Small_[Size_] = value;
                else {
                    if (Size_ == 16) Large_.assign(Small_, Small_ + 16);
                    Large_.push_back(value);
                }
                ++Size_;
            }
        private:
            size_t Size_{};
            TIndexType Small_[16];
            TVector<TIndexType> Large_;
        };

        TIndexType Pack(ECompactKind kind, TIndexType index) {
            Y_ENSURE(index <= CompactMaxIndex, "Compact JSON exceeds the index limit");
            return (index << CompactKindBits) | kind;
        }

        // Fixed-size slabs avoid per-container allocations, geometric capacity
        // waste and a second full-size buffer on final shrink/freeze.
        template <class T>
        class TPool {
        public:
            TIndexType Add(const T& value) {
                Y_ENSURE(Size_ <= CompactMaxIndex, "Compact JSON pool is too large");
                if ((Size_ & 4095) == 0) {
                    Chunks_.emplace_back(new T[4096]);
                }
                const TIndexType result = Size_++;
                At(result) = value;
                return result;
            }
            template <class TValues>
            TIndexType Append(const TValues& values) {
                const TIndexType start = Size_;
                for (const auto& value : values) {
                    Add(value);
                }
                return start;
            }
            T& At(TIndexType index) { return Chunks_[index >> 12][index & 4095]; }
            const T& At(TIndexType index) const { return Chunks_[index >> 12][index & 4095]; }
            TIndexType Size() const { return Size_; }
            size_t Bytes() const { return Chunks_.size() * 4096 * sizeof(T) + Chunks_.capacity() * sizeof(void*); }
        private:
            TIndexType Size_{};
            TVector<std::unique_ptr<T[]>> Chunks_;
        };

        ui64 Mix(ui64 hash, TIndexType value) {
            return (hash ^ value) * 1099511628211ull;
        }

        PyObject* MakeString(TStringBuf value, bool intern) {
#if PY_MAJOR_VERSION == 3
            PyObject* result = PyUnicode_FromStringAndSize(value.data(), value.size());
            if (result && intern) PyUnicode_InternInPlace(&result);
#else
            PyObject* result = PyString_FromStringAndSize(value.data(), value.size());
            if (result && intern) PyString_InternInPlace(&result);
#endif
            Y_ENSURE(result, "Cannot create compact JSON string");
            return result;
        }
    }

    struct TCompactStorage::TImpl {
        struct TList { TIndexType Offset{}; TIndexType Size{}; };
        struct TMap { TIndexType Shape{}; TIndexType Offset{}; };
        struct TShape { TIndexType Offset{}; TIndexType Size{}; TIndexType IndexOffset{}; TIndexType IndexMask{}; };
        struct TBuildIndex {
            TVector<TIndexType> Strings;
            std::unordered_map<ui64, TVector<TIndexType>> Shapes;
            TVector<TIndexType> Maps = TVector<TIndexType>(CacheSize, CompactEmptyIndex);
            TVector<TIndexType> Lists = TVector<TIndexType>(CacheSize, CompactEmptyIndex);
            size_t MapEntries{};
            size_t ListEntries{};
        };

        bool InternKeys;
        bool InternValues;
        TPool<PyObject*> Strings;
        TPool<TIndexType> Values;
        TPool<TIndexType> Keys;
        TPool<TIndexType> KeyIndex;
        TPool<TList> Lists;
        TPool<TMap> Maps;
        TPool<TShape> Shapes;
        TPool<ui64> Numbers;
        size_t Shared{};
        std::unique_ptr<TBuildIndex> Build = std::make_unique<TBuildIndex>();

        TImpl(bool keys, bool values) : InternKeys(keys), InternValues(values) {}
        ~TImpl() {
            for (TIndexType i = 0; i < Strings.Size(); ++i) Py_DECREF(Strings.At(i));
        }

        size_t StringHash(PyObject* value) const {
            if (InternKeys && InternValues) {
                // Interning already guarantees canonical identity. Avoid a
                // second Unicode hash/equality lookup for every graph edge.
                ui64 pointer = reinterpret_cast<uintptr_t>(value) >> 4;
                pointer ^= pointer >> 33;
                pointer *= 0xff51afd7ed558ccdull;
                pointer ^= pointer >> 33;
                return pointer;
            }
            return static_cast<size_t>(PyObject_Hash(value));
        }

        TIndexType AddString(TStringBuf text, bool intern) {
            PyObject* candidate = MakeString(text, intern);
            auto& index = Build->Strings;
            if (index.empty() || (Strings.Size() + 1) * 4 >= index.size() * 3) {
                TVector<TIndexType> next(index.empty() ? 16 : index.size() * 2, CompactEmptyIndex);
                for (TIndexType i = 0; i < Strings.Size(); ++i) {
                    size_t slot = StringHash(Strings.At(i)) & (next.size() - 1);
                    while (next[slot] != CompactEmptyIndex) slot = (slot + 1) & (next.size() - 1);
                    next[slot] = i;
                }
                index.swap(next);
            }
            size_t slot = StringHash(candidate) & (index.size() - 1);
            while (index[slot] != CompactEmptyIndex) {
                PyObject* old = Strings.At(index[slot]);
                const int equal = old == candidate ? 1 : InternKeys && InternValues ? 0 : PyObject_RichCompareBool(old, candidate, Py_EQ);
                if (equal != 0) {
                    Py_DECREF(candidate);
                    Y_ENSURE(equal > 0, "Cannot compare compact strings");
                    return index[slot];
                }
                slot = (slot + 1) & (index.size() - 1);
            }
            TIndexType id;
            try {
                id = Strings.Add(candidate);
            } catch (...) {
                Py_DECREF(candidate);
                throw;
            }
            index[slot] = id;
            return id;
        }

        TIndexType Shape(const TInlineValues& keys) {
            ui64 hash = Mix(1469598103934665603ull, keys.size());
            for (TIndexType key : keys) hash = Mix(hash, key);
            auto& candidates = Build->Shapes[hash];
            for (TIndexType id : candidates) {
                const auto& shape = Shapes.At(id);
                bool equal = shape.Size == keys.size();
                for (TIndexType i = 0; equal && i < shape.Size; ++i) equal = Keys.At(shape.Offset + i) == keys[i];
                if (equal) return id;
            }
            TShape shape{Keys.Append(keys), static_cast<TIndexType>(keys.size()), 0, 0};
            if (keys.size() > 16) {
                size_t count = 32;
                while (count < keys.size() * 2) count *= 2;
                TVector<TIndexType> slots(count, CompactEmptyIndex);
                for (TIndexType i = 0; i < keys.size(); ++i) {
                    size_t slot = static_cast<size_t>(PyObject_Hash(Strings.At(keys[i]))) & (count - 1);
                    while (slots[slot] != CompactEmptyIndex) slot = (slot + 1) & (count - 1);
                    slots[slot] = i;
                }
                shape.IndexOffset = KeyIndex.Append(slots);
                shape.IndexMask = count - 1;
            }
            const TIndexType id = Shapes.Add(shape);
            candidates.push_back(id);
            return id;
        }

        TIndexType Block(const TInlineValues& values, TIndexType shape, TIndexType id, bool map) {
            if (values.empty()) return 0;
            if (values.size() <= 8 && std::none_of(values.begin(), values.end(), [](TIndexType value) {
                return (value & CompactKindMask) == Map || (value & CompactKindMask) == List;
            })) {
                ui64 hash = Mix(1469598103934665603ull, shape);
                for (TIndexType value : values) hash = Mix(hash, value);
                auto& cache = map ? Build->Maps : Build->Lists;
                auto& entries = map ? Build->MapEntries : Build->ListEntries;
                size_t slot = hash & (cache.size() - 1);
                while (cache[slot] != CompactEmptyIndex) {
                    const TIndexType old = cache[slot];
                    const TIndexType offset = map ? Maps.At(old).Offset : Lists.At(old).Offset;
                    bool equal = map ? Maps.At(old).Shape == shape : Lists.At(old).Size == values.size();
                    for (size_t i = 0; equal && i < values.size(); ++i) equal = Values.At(offset + i) == values[i];
                    if (equal) {
                        ++Shared;
                        return offset;
                    }
                    slot = (slot + 1) & (cache.size() - 1);
                }
                const TIndexType offset = Values.Append(values);
                if (entries < cache.size() / 2) {
                    cache[slot] = id;
                    ++entries;
                }
                return offset;
            }
            return Values.Append(values);
        }
    };

    TCompactStorage::TCompactStorage(bool keys, bool values) : Impl_(new TImpl(keys, values)) {}
    TCompactStorage::~TCompactStorage() = default;
    size_t TCompactStorage::Size(TIndexType value) const {
        return (value & CompactKindMask) == List ? Impl_->Lists.At(value >> CompactKindBits).Size
            : Impl_->Shapes.At(Impl_->Maps.At(value >> CompactKindBits).Shape).Size;
    }
    TIndexType TCompactStorage::Item(TIndexType list, size_t index) const {
        return Impl_->Values.At(Impl_->Lists.At(list >> CompactKindBits).Offset + index);
    }
    TIndexType TCompactStorage::MapValue(TIndexType map, size_t index) const {
        return Impl_->Values.At(Impl_->Maps.At(map >> CompactKindBits).Offset + index);
    }
    PyObject* TCompactStorage::MapKey(TIndexType map, size_t index) const {
        const auto& shape = Impl_->Shapes.At(Impl_->Maps.At(map >> CompactKindBits).Shape);
        PyObject* key = Impl_->Strings.At(Impl_->Keys.At(shape.Offset + index));
        Py_INCREF(key);
        return key;
    }
    bool TCompactStorage::MapGet(TIndexType map, PyObject* key, TIndexType* value) const {
        const auto& item = Impl_->Maps.At(map >> CompactKindBits);
        const auto& shape = Impl_->Shapes.At(item.Shape);
        auto equal = [&](TIndexType position) {
            PyObject* stored = Impl_->Strings.At(Impl_->Keys.At(shape.Offset + position));
            const int result = key == stored ? 1 : PyObject_RichCompareBool(key, stored, Py_EQ);
            Y_ENSURE(result >= 0, "Cannot compare mapping key");
            return result != 0;
        };
        if (shape.IndexMask) {
            const auto hash = PyObject_Hash(key);
            Y_ENSURE(hash != -1 || !PyErr_Occurred(), "Unhashable mapping key");
            TIndexType slot = static_cast<size_t>(hash) & shape.IndexMask;
            while (true) {
                const TIndexType position = Impl_->KeyIndex.At(shape.IndexOffset + slot);
                if (position == CompactEmptyIndex) return false;
                if (equal(position)) { *value = Impl_->Values.At(item.Offset + position); return true; }
                slot = (slot + 1) & shape.IndexMask;
            }
        }
        // Common graph fields are interned strings: this loop is a handful of
        // pointer comparisons and needs neither a per-node hash table nor hash().
        for (TIndexType i = 0; i < shape.Size; ++i) {
            if (key == Impl_->Strings.At(Impl_->Keys.At(shape.Offset + i))) {
                *value = Impl_->Values.At(item.Offset + i);
                return true;
            }
        }
        const auto hash = PyObject_Hash(key);
        Y_ENSURE(hash != -1 || !PyErr_Occurred(), "Unhashable mapping key");
        for (TIndexType i = 0; i < shape.Size; ++i) {
            PyObject* stored = Impl_->Strings.At(Impl_->Keys.At(shape.Offset + i));
            if (PyObject_Hash(stored) == hash && equal(i)) {
                *value = Impl_->Values.At(item.Offset + i);
                return true;
            }
        }
        return false;
    }
    long long TCompactStorage::Signed(TIndexType value) const { return static_cast<long long>(Impl_->Numbers.At(value >> CompactKindBits)); }
    unsigned long long TCompactStorage::Unsigned(TIndexType value) const { return Impl_->Numbers.At(value >> CompactKindBits); }
    double TCompactStorage::Float(TIndexType value) const {
        ui64 bits = Impl_->Numbers.At(value >> CompactKindBits);
        double result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }
    PyObject* TCompactStorage::StringObject(TIndexType value) const { return Impl_->Strings.At(value >> CompactKindBits); }
    PyObject* TCompactStorage::Scalar(TIndexType value) const {
        switch (value & CompactKindMask) {
            case Special:
                if (value == 0) Py_RETURN_NONE;
                if ((value >> CompactKindBits) == 1) Py_RETURN_FALSE;
                Py_RETURN_TRUE;
            case SmallInt: {
                const TIndexType data = value >> CompactKindBits;
                return PyLong_FromLongLong(static_cast<long long>(data >> 1) ^ -static_cast<long long>(data & 1));
            }
            case String: { PyObject* result = StringObject(value); Py_INCREF(result); return result; }
            case Int64: return PyLong_FromLongLong(Signed(value));
            case UInt64: return PyLong_FromUnsignedLongLong(Unsigned(value));
            case Double: return PyFloat_FromDouble(Float(value));
            default: PyErr_SetString(PyExc_TypeError, "Not a compact scalar"); return nullptr;
        }
    }
    void TCompactStorage::Finish() { Impl_->Build.reset(); }
    size_t TCompactStorage::StringCount() const { return Impl_->Strings.Size(); }
    size_t TCompactStorage::ListCount() const { return Impl_->Lists.Size(); }
    size_t TCompactStorage::MapCount() const { return Impl_->Maps.Size(); }
    size_t TCompactStorage::ShapeCount() const { return Impl_->Shapes.Size(); }
    size_t TCompactStorage::ValueCount() const { return Impl_->Values.Size(); }
    size_t TCompactStorage::SharedBlocks() const { return Impl_->Shared; }
    size_t TCompactStorage::NativeBytes() const {
        return sizeof(TImpl) + Impl_->Strings.Bytes() + Impl_->Values.Bytes() + Impl_->Keys.Bytes()
            + Impl_->KeyIndex.Bytes() + Impl_->Lists.Bytes() + Impl_->Maps.Bytes() + Impl_->Shapes.Bytes() + Impl_->Numbers.Bytes();
    }

    struct TCompactBuilder::TImpl {
        struct TFrame {
            bool Map;
            TIndexType Id;
            TIndexType Pending{};
            TInlineValues Keys;
            TInlineValues Values;
            TVector<TIndexType> Index;
            bool AwaitingValue{};
            TIndexType Key(TIndexType key) {
                if (Keys.size() < 16) {
                    for (TIndexType i = 0; i < Keys.size(); ++i) if (Keys[i] == key) return i;
                } else {
                    if (Index.empty() || (Keys.size() + 1) * 2 >= Index.size()) {
                        TVector<TIndexType> next(Index.empty() ? 64 : Index.size() * 2, CompactEmptyIndex);
                        for (TIndexType i = 0; i < Keys.size(); ++i) {
                            size_t slot = (Keys[i] * 2654435761u) & (next.size() - 1);
                            while (next[slot] != CompactEmptyIndex) slot = (slot + 1) & (next.size() - 1);
                            next[slot] = i;
                        }
                        Index.swap(next);
                    }
                    size_t slot = (key * 2654435761u) & (Index.size() - 1);
                    while (Index[slot] != CompactEmptyIndex) {
                        if (Keys[Index[slot]] == key) return Index[slot];
                        slot = (slot + 1) & (Index.size() - 1);
                    }
                    Index[slot] = Keys.size();
                }
                const TIndexType position = Keys.size();
                Keys.push_back(key);
                Values.push_back(0);
                return position;
            }
        };
        TCompactStorage::TImpl& Store;
        bool Project;
        const THashSet<TString>& Fields;
        TVector<TFrame> Frames;
        bool Skipping{};
        size_t SkipDepth{};
        TIndexType Result{};
        bool Complete{};

        TImpl(TCompactStorage::TImpl& store, bool project, const THashSet<TString>& fields)
            : Store(store), Project(project), Fields(fields) {}
        bool SkipScalar() {
            if (!Skipping) return false;
            if (!SkipDepth) Skipping = false;
            return true;
        }
        void Add(TIndexType value) {
            if (Frames.empty()) { Result = value; Complete = true; return; }
            auto& parent = Frames.back();
            if (parent.Map) {
                Y_ENSURE(parent.AwaitingValue, "Expected a compact JSON map key");
                parent.Values[parent.Pending] = value;
                parent.AwaitingValue = false;
            } else parent.Values.push_back(value);
        }
        bool Open(bool map) {
            if (Skipping) { ++SkipDepth; return true; }
            Y_ENSURE(Frames.empty() || !Frames.back().Map || Frames.back().AwaitingValue,
                     "Expected a compact JSON map key");
            const TIndexType id = map ? Store.Maps.Add({}) : Store.Lists.Add({});
            Frames.push_back(TFrame{map, id, 0, {}, {}, {}});
            return true;
        }
        bool Close(bool map) {
            if (Skipping) {
                Y_ENSURE(SkipDepth, "Expected a compact JSON value");
                if (!--SkipDepth) Skipping = false;
                return true;
            }
            Y_ENSURE(!Frames.empty() && Frames.back().Map == map, "Mismatched compact JSON container");
            Y_ENSURE(!Frames.back().AwaitingValue, "Expected a compact JSON map value");
            TFrame frame = std::move(Frames.back());
            Frames.pop_back();
            if (map) {
                auto& record = Store.Maps.At(frame.Id);
                record.Shape = Store.Shape(frame.Keys);
                record.Offset = Store.Block(frame.Values, record.Shape, frame.Id, true);
            } else {
                auto& record = Store.Lists.At(frame.Id);
                record.Size = frame.Values.size();
                record.Offset = Store.Block(frame.Values, 0, frame.Id, false);
            }
            Add(Pack(map ? Map : List, frame.Id));
            return true;
        }
    };

    TCompactBuilder::TCompactBuilder(TCompactStorage& storage, bool project, const THashSet<TString>& fields)
        : TJsonCallbacks(true), Impl_(new TImpl(*storage.Impl_, project, fields)) {}
    TCompactBuilder::~TCompactBuilder() = default;
    bool TCompactBuilder::OnNull() { if (!Impl_->SkipScalar()) Impl_->Add(0); return true; }
    bool TCompactBuilder::OnBoolean(bool value) { if (!Impl_->SkipScalar()) Impl_->Add(Pack(Special, value ? 2 : 1)); return true; }
    bool TCompactBuilder::OnInteger(long long value) {
        if (Impl_->SkipScalar()) return true;
        if (value >= -static_cast<i64>(CompactSmallIntLimit) && value < static_cast<i64>(CompactSmallIntLimit)) {
            Impl_->Add(Pack(SmallInt, static_cast<TIndexType>((static_cast<ui64>(value) << 1) ^ static_cast<ui64>(value >> 63))));
        } else Impl_->Add(Pack(Int64, Impl_->Store.Numbers.Add(static_cast<ui64>(value))));
        return true;
    }
    bool TCompactBuilder::OnUInteger(unsigned long long value) {
        if (Impl_->SkipScalar()) return true;
        if (value < CompactSmallIntLimit) Impl_->Add(Pack(SmallInt, value << 1));
        else Impl_->Add(Pack(UInt64, Impl_->Store.Numbers.Add(value)));
        return true;
    }
    bool TCompactBuilder::OnDouble(double value) {
        if (Impl_->SkipScalar()) return true;
        ui64 bits;
        std::memcpy(&bits, &value, sizeof(bits));
        Impl_->Add(Pack(Double, Impl_->Store.Numbers.Add(bits)));
        return true;
    }
    bool TCompactBuilder::OnString(const TStringBuf& value) {
        if (!Impl_->SkipScalar()) Impl_->Add(Pack(String, Impl_->Store.AddString(value, Impl_->Store.InternValues)));
        return true;
    }
    bool TCompactBuilder::OnOpenArray() { return Impl_->Open(false); }
    bool TCompactBuilder::OnOpenMap() { return Impl_->Open(true); }
    bool TCompactBuilder::OnCloseArray() { return Impl_->Close(false); }
    bool TCompactBuilder::OnCloseMap() { return Impl_->Close(true); }
    bool TCompactBuilder::OnMapKey(const TStringBuf& key) {
        if (Impl_->Skipping) return true;
        Y_ENSURE(!Impl_->Frames.empty() && Impl_->Frames.back().Map, "Unexpected compact JSON map key");
        Y_ENSURE(!Impl_->Frames.back().AwaitingValue, "Expected a compact JSON map value");
        if (Impl_->Project && Impl_->Frames.size() == 2 && !Impl_->Frames[0].Map &&
            !Impl_->Fields.contains(TString(key))) {
            Impl_->Skipping = true;
            return true;
        }
        auto& frame = Impl_->Frames.back();
        frame.Pending = frame.Key(Impl_->Store.AddString(key, Impl_->Store.InternKeys));
        frame.AwaitingValue = true;
        return true;
    }
    bool TCompactBuilder::Done() const { return Impl_->Complete; }
    TIndexType TCompactBuilder::Result() const { return Impl_->Result; }
}
