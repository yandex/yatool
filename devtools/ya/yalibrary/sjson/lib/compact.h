#pragma once

#include <Python.h>
#include <library/cpp/json/common/defs.h>
#include <util/generic/hash_set.h>
#include <util/generic/string.h>

#include <limits>
#include <memory>
#include <type_traits>

namespace NSJson {
    using TIndexType = ui32;
    static_assert(std::is_unsigned_v<TIndexType> &&
                  (std::numeric_limits<TIndexType>::digits == 32 || std::numeric_limits<TIndexType>::digits == 64));

    constexpr unsigned CompactKindBits = 3;
    constexpr TIndexType CompactKindMask = (TIndexType{1} << CompactKindBits) - 1;
    constexpr TIndexType CompactMaxIndex = std::numeric_limits<TIndexType>::max() >> CompactKindBits;
    constexpr TIndexType CompactEmptyIndex = std::numeric_limits<TIndexType>::max();
    constexpr ui64 CompactSmallIntLimit = (static_cast<ui64>(CompactMaxIndex) >> 1) + 1;

    // Low three bits identify the type; the remaining bits are an index or a
    // zig-zag encoded small integer. No PyObject is allocated for a container.
    enum ECompactKind : TIndexType { Special, SmallInt, String, List, Map, Int64, UInt64, Double };
    static_assert(Double <= CompactKindMask);

    class TCompactStorage {
    public:
        TCompactStorage(bool internKeys, bool internValues);
        ~TCompactStorage();
        TCompactStorage(const TCompactStorage&) = delete;
        TCompactStorage& operator=(const TCompactStorage&) = delete;

        size_t Size(TIndexType value) const;
        TIndexType Item(TIndexType list, size_t index) const;
        TIndexType MapValue(TIndexType map, size_t index) const;
        PyObject* MapKey(TIndexType map, size_t index) const;
        bool MapGet(TIndexType map, PyObject* key, TIndexType* value) const;
        PyObject* Scalar(TIndexType value) const;
        long long Signed(TIndexType value) const;
        unsigned long long Unsigned(TIndexType value) const;
        double Float(TIndexType value) const;
        PyObject* StringObject(TIndexType value) const;
        void Finish();

        size_t NativeBytes() const;
        size_t StringCount() const;
        size_t ListCount() const;
        size_t MapCount() const;
        size_t ShapeCount() const;
        size_t ValueCount() const;
        size_t SharedBlocks() const;

    private:
        struct TImpl;
        std::unique_ptr<TImpl> Impl_;
        friend class TCompactBuilder;
    };

    class TCompactBuilder : public NJson::TJsonCallbacks {
    public:
        TCompactBuilder(TCompactStorage&, bool projectItems, const THashSet<TString>& fields);
        ~TCompactBuilder();
        bool OnNull() override;
        bool OnBoolean(bool) override;
        bool OnInteger(long long) override;
        bool OnUInteger(unsigned long long) override;
        bool OnDouble(double) override;
        bool OnString(const TStringBuf&) override;
        bool OnOpenArray() override;
        bool OnCloseArray() override;
        bool OnOpenMap() override;
        bool OnCloseMap() override;
        bool OnMapKey(const TStringBuf&) override;
        bool Done() const;
        TIndexType Result() const;

    private:
        struct TImpl;
        std::unique_ptr<TImpl> Impl_;
    };

    using TCompactOverride = PyObject* (*)(PyObject*, TIndexType);
    void EncodeCompact(PyObject* stream, const TCompactStorage&, TIndexType value,
                       PyObject* owner, TCompactOverride overrideValue);
}
