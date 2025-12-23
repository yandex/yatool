#pragma once

#include <devtools/ymake/diag/dbg.h>

#include <library/cpp/containers/absl_flat_hash/flat_hash_map.h>
#include <library/cpp/containers/absl_flat_hash/flat_hash_set.h>

#include <util/generic/algorithm.h>
#include <util/generic/hash_set.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>

#include <util/system/yassert.h>

#include <type_traits>

/*
    Container that looks like a simple collection of elements with some additional features/guarantees:
      * stores only unique elements;
      * preserves the order of insertion;
      * that's why provides only fixed list of modifying methods;
      * (optionally) provides some index-aware methods: e.g. fast finding, replacing, partial updating.

    N.B.:
      * uniqueness is controlled thru additional data type (TRef) that acts like a functor
        and represents significant part of each element. Also TRef can reduce memory footprint
        (see internal notes);
      * you must not modify TRef part of an element in Update method otherwise assertion will fail;
      * Replacing an element can lead to a conflict of uniqueness. In this case the coresponding Replace
        method fails and returns false;
      * Push method tries to add new element to the end: returns false if the element already existed,
        true otherwise. In "indexed" version also returns the index of the element.

    Internally:
      * maintains two containers: TContainer (the main one, underlying) which stores elements and
        THashSet of TRefs (or THashMap in "indexed" version) to control uniqueness and to support lookups
        with the good complexity: constant on average;
      * any TRef is constructed with the reference of an inserted element. So it can store just
        a pointer/reference to the element or can be much lighter object than original one;
      * THashSet/Map is consructed lazy: when a number of elements exceeds defined threshold HashTh;
      * HashTh value depends on many things: not only the sizeof T but usage patterns of TUniqContainerImpl
        that you've already had. So the only way to find suitable/perfect values of HashTh is to do
        lots of real benchmarks.
*/
template <class T, class TRef, size_t HashTh, class TContainer = TVector<T>, bool IsIndexed = false>
class TUniqContainerImpl {
protected:
    using TUtilUniqMap = typename std::conditional_t<IsIndexed, THashMap<TRef, size_t>, THashSet<TRef>>;
    using TAbslUniqMap = typename std::conditional_t<IsIndexed, absl::flat_hash_map<TRef, size_t, THash<TRef>, TEqualTo<TRef>>, absl::flat_hash_set<TRef, THash<TRef>, TEqualTo<TRef>>>;
    static constexpr bool UseAbseil = sizeof(T) <= 8;
    static constexpr bool IsSimpleRef = std::is_constructible_v<TRef, T> || std::is_same_v<TRef, std::reference_wrapper<const T>>;
    using TUniqMap = typename std::conditional_t<UseAbseil, TAbslUniqMap, TUtilUniqMap>;
    using TReturnType = typename std::conditional_t<IsIndexed, std::pair<size_t, bool>, bool>;

public:
    using const_iterator = typename TContainer::const_iterator;

    const_iterator begin() const noexcept {
        return Container.cbegin();
    }

    const_iterator end() const noexcept {
        return Container.cend();
    }

    const_iterator cbegin() const noexcept {
        return Container.cbegin();
    }

    const_iterator cend() const noexcept {
        return Container.cend();
    }

    bool empty() const noexcept {
        return Container.empty();
    }

    template <class U>
    bool has(const U& val) const {
        return !UniqMap ? AnyOf(Container, InContainerPred(val)) : UniqMap->contains(val);
    }

    template <class U, bool Enable = IsIndexed>
    typename std::enable_if_t<Enable, size_t> Index(const U& val) const {
        if (!UniqMap) {
            return FindIndexIf(Container, InContainerPred(val));
        } else {
            const auto it = UniqMap->find(val);
            return it != UniqMap->end() ? it->second : NPOS;
        }
    }

    template <class F, bool Enable = IsIndexed>
    typename std::enable_if_t<Enable, void> Update(const size_t i, F&& f) {
        Y_ASSERT(i < Container.size());
        T& val = Container[i];
        if constexpr (IsSimpleRef) {
#ifndef NDEBUG
            const T valCopy = val;
            const TRef refBefore(valCopy);
#endif
            f(val);
#ifndef NDEBUG
            const TRef refAfter(val);
            Y_ASSERT(refAfter == refBefore);
#endif
        } else {
            f(val);
        }
    }

    template <bool Enable = IsIndexed>
    typename std::enable_if_t<Enable, bool> Replace(const size_t i, const T& newVal) {
        Y_ASSERT(i < Container.size());
        if (!UniqMap) {
            const size_t newIdx = FindIndexIf(Container, InContainerPred(newVal));
            if (newIdx != NPOS && newIdx != i)
                return false;
            Container[i] = newVal;
        } else {
            if constexpr (UseAbseil) {
                const auto it = UniqMap->find(newVal);
                if (it != UniqMap->end() && it->second != i)
                    return false;
                auto& val = Container[i];
                UniqMap->erase(val);
                val = newVal;
                UniqMap->emplace(val, i);
            } else {
                typename TUniqMap::insert_ctx ins;
                const auto it = UniqMap->find(newVal, ins);
                if (it != UniqMap->end() && it->second != i)
                    return false;
                auto& val = Container[i];
                UniqMap->erase(val);
                val = newVal;
                if constexpr (IsSimpleRef) {
                    UniqMap->emplace_direct(ins, val, i);
                } else {
                    UniqMap->emplace_direct(ins, i, i);
                }
            }
        }
        return true;
    }

    TReturnType Push(const T& val) {
        T rval = val;
        return Push(std::move(rval));
    }

    TReturnType Push(T&& val) {
        if (!UniqMap) {
            if constexpr (IsIndexed) {
                size_t idx = FindIndexIf(Container, InContainerPred(val));
                if (idx != NPOS)
                    return {idx, false};
                idx = Container.size();
                Container.emplace_back(std::move(val));
                CreateUniqMapIfNeeded();
                return {idx, true};
            } else {
                if (AnyOf(Container, InContainerPred(val)))
                    return false;
                Container.emplace_back(std::move(val));
                CreateUniqMapIfNeeded();
                return true;
            }
        } else {
            if constexpr (UseAbseil) {
                bool added = false;
                if constexpr (IsIndexed) {
                    const auto it = UniqMap->lazy_emplace(val, [&,this](const auto& ctor) {
                        const size_t idx = Container.size();
                        Container.emplace_back(std::move(val));
                        if constexpr (IsSimpleRef) {
                            ctor(Container.back(), idx);
                        } else {
                            ctor(idx, idx);
                        }
                        added = true;
                    });
                    return {it->second, added};
                } else {
                    UniqMap->lazy_emplace(val, [&,this](const auto& ctor) {
                        Container.emplace_back(std::move(val));
                        if constexpr (IsSimpleRef) {
                            ctor(Container.back());
                        } else {
                            ctor(Container.size() - 1);
                        }
                        added = true;
                    });
                    return added;
                }
            } else {
                typename TUniqMap::insert_ctx ins;
                const auto it = UniqMap->find(val, ins);
                if (it != UniqMap->end()) {
                    if constexpr (IsIndexed)
                        return {it->second, false};
                    else
                        return false;
                }
                if constexpr (IsIndexed) {
                    const size_t idx = Container.size();
                    Container.emplace_back(std::move(val));
                    if constexpr (IsSimpleRef) {
                        UniqMap->emplace_direct(ins, Container.back(), idx);
                    } else {
                        UniqMap->emplace_direct(ins, idx, idx);
                    }
                    return {idx, true};
                } else {
                    Container.emplace_back(std::move(val));
                    if constexpr (IsSimpleRef) {
                        UniqMap->emplace_direct(ins, Container.back());
                    } else {
                        UniqMap->emplace_direct(ins, Container.size() - 1);
                    }
                    return true;
                }
            }
        }
    }

    const TContainer& Data() const noexcept {
        return Container;
    }

    TContainer Take() noexcept(std::is_nothrow_swappable_v<TContainer> && std::is_nothrow_move_constructible_v<TContainer>) {
        UniqMap.Reset();
        TContainer res;
        std::swap(res, Container);
        return res;
    }

    const T& operator[](size_t i) const {
        Y_ASSERT(i < Container.size());
        return Container[i];
    }

    void clear() {
        Check();
        Container.clear();
        UniqMap.Reset();
    }

    size_t size() const {
        return Container.size();
    }

    void reserve(size_t new_cap) {
        Container.reserve(new_cap);
        // It's rather debatable whether we need to reserve in UniqMap or not.
    }

    void swap(TUniqContainerImpl& other) {
        Container.swap(other.Container);
        if (UniqMap) {
            if (other.UniqMap)
                UniqMap->swap(*other.UniqMap);
            else {
                other.CreateEmptyUniqMap();
                UniqMap->swap(*other.UniqMap);
                UniqMap.Reset();
            }
        } else {
            if (other.UniqMap) {
                CreateEmptyUniqMap();
                UniqMap->swap(*other.UniqMap);
                other.UniqMap.Reset();
            }
        }
    }

    bool IsSubsetOf(const TUniqContainerImpl& of) const {
        if (size() > of.size()) {
            return false;
        }
        return AllOf(Container, [&of](const auto& val) { return of.has(val); });
    }

    void AddTo(TUniqContainerImpl& to) const {
        for (const auto& val : Container) {
            to.Push(val);
        }
    }

    void CopyTo(TUniqContainerImpl& to) const {
        TUniqContainerImpl tmp(*this);
        to.swap(tmp);
    }

    bool operator==(const TUniqContainerImpl& other) const {
        return Container == other.Container;
    }

    // It would be great to delete copy ctor...
    TUniqContainerImpl(const TUniqContainerImpl& from)
        : Container(from.Container)
    {
        CreateUniqMapIfNeeded();
    }

    TUniqContainerImpl& operator=(const TUniqContainerImpl&) = delete;

    TUniqContainerImpl(TUniqContainerImpl&& from) = default;
    TUniqContainerImpl& operator=(TUniqContainerImpl&&) = default;

    TUniqContainerImpl() = default;

    ~TUniqContainerImpl() {
        Check();
    }

private:
    TContainer Container;
    THolder<TUniqMap> UniqMap;

    template <class U>
    auto InContainerPred(const U& val) const {
        return [&val](const T& e) {
            if constexpr ((std::is_constructible_v<TRef, U> || std::is_same_v<TRef, std::reference_wrapper<const U>>) && IsSimpleRef) {
                return TRef(e) == TRef(val);
            } else {
                return e == val;
            }
        };
    }

    void CreateUniqMapIfNeeded() {
        if (size() <= HashTh)
            return;
        if constexpr (IsIndexed) {
            if constexpr (IsSimpleRef) {
                UniqMap = MakeHolder<TUniqMap>(size());
                size_t i = 0;
                for (const auto& val : Container)
                    UniqMap->emplace(val, i++);
            } else {
                UniqMap = MakeHolder<TUniqMap>(size(), THash<TRef>(Container), TEqualTo<TRef>(Container));
                for (size_t i = 0; i < size(); ++i) {
                    UniqMap->emplace(i, i);
                }
            }
        } else {
            if constexpr (IsSimpleRef) {
                UniqMap = MakeHolder<TUniqMap>(cbegin(), cend(), size());
            } else {
                UniqMap = MakeHolder<TUniqMap>(size(), THash<TRef>(Container), TEqualTo<TRef>(Container));
                size_t i = 0;
                for (const auto& val : Container) {
                    if constexpr (UseAbseil) {
                        UniqMap->lazy_emplace(val, [&](const auto& ctor) {
                            ctor(i);
                        });
                    } else {
                        UniqMap->emplace(i);
                    }
                    ++i;
                }
            }
        }
    }

    void CreateEmptyUniqMap() {
        Y_DEBUG_ABORT_UNLESS(!UniqMap);
        if constexpr (IsSimpleRef) {
            UniqMap = MakeHolder<TUniqMap>(0);
        } else {
            UniqMap = MakeHolder<TUniqMap>(0, THash<TRef>(Container), TEqualTo<TRef>(Container));
        }
    }

    void Check() {
#ifndef NDEBUG
        if (UniqMap && UniqMap->size() != Container.size()) {
            Dump();
            AssertEx(false, "Different sizes: " << UniqMap->size() << " and " << Container.size());
        }

        if (UniqMap) {
            size_t i = 0;
            for (const auto& val : Container) {
                auto it = UniqMap->find(val);
                if (it == UniqMap->end()) {
                    Dump();
                    if constexpr (IsSimpleRef) {
                        AssertEx(false, "No key " << TRef(val) << " in map");
                    } else {
                        AssertEx(false, "No key " << TRef::ToString(Container, i) << " in map");
                    }
                }
                if constexpr (IsIndexed) {
                    if (it->second != i) {
                        Dump();
                        if constexpr (IsSimpleRef) {
                            AssertEx(false, "For key " << TRef(val) << " different indexes: " << it->second << " != " << i);
                        } else {
                            AssertEx(false, "For key " << TRef::ToString(Container, i) << " different indexes: " << it->second << " != " << i);
                        }
                    }
                }
                i++;
            }
        }
#endif  // NDEBUG
    }

#ifndef NDEBUG
    void Dump() {
        Cerr << "Container: ";
        if (Container.empty()) {
            Cerr << " empty" << Endl;
        } else {
            Cerr << Endl;
            size_t i = 0;
            for (const auto& v : Container) {
                if constexpr (IsSimpleRef) {
                    Cerr << "\t" << i << "\t" << TRef(v) << Endl;
                } else {
                    Cerr << "\t" << i << "\t" << TRef::ToString(Container, i) << Endl;
                }
                i++;
            }
        }
        Cerr << "Map:";
        if (!UniqMap) {
            Cerr << " null" << Endl;
        } else if (UniqMap->empty()) {
            Cerr << " empty" << Endl;
        } else {
            Cerr << Endl;
            for (const auto& v : *UniqMap) {
                if constexpr (IsIndexed) {
                    if constexpr (IsSimpleRef) {
                        Cerr << "\t" << v.first << "\t" << v.second << Endl;
                    } else {
                        Y_ASSERT(v.first.index < Container.size());
                        Cerr << "\t" << TRef::ToString(Container, v.first) << "\t" << v.second << Endl;
                    }
                } else if constexpr (IsSimpleRef) {
                    Cerr << "\t" << v << Endl;
                } else {
                    Y_ASSERT(v.index < Container.size());
                    Cerr << "\t" << TRef::ToString(Container, v) << Endl;
                }
            }
        }
    }
#endif  // NDEBUG
};

namespace NUniqContainer {
    template <class T>
    using TRefWrapper = std::reference_wrapper<const T>;

    template <class T>
    class TRefWithIndex {
    public:
        TRefWithIndex(size_t id) : index(id) {}
        static std::string ToString(const TVector<T>& vec, size_t index) {
            return vec[index];
        }
        static std::string ToString(const TVector<T>& vec, const TRefWithIndex<T>& ref) {
            return vec[ref.index];
        }

    public:
        size_t index;
    };
}

template <class T>
struct THash<NUniqContainer::TRefWrapper<T>> {
    size_t operator()(NUniqContainer::TRefWrapper<T> ref) const {
        const THash<T> hash;
        return hash(ref.get());
    }
};

template <class T>
bool operator==(NUniqContainer::TRefWrapper<T> r1, NUniqContainer::TRefWrapper<T> r2) {
    return r1.get() == r2.get();
}

template <class T>
struct THash<NUniqContainer::TRefWithIndex<T>> {
    using is_transparent = void;
    explicit THash(const TVector<T>& ref) : vector_ref(ref) {}

    size_t operator()(const NUniqContainer::TRefWithIndex<T>& ref) const {
        const THash<T> hash;
        Y_ASSERT(ref.index < vector_ref.size());
        return hash(vector_ref[ref.index]);
    }
    template <class U>
    size_t operator()(const U& t) const {
        const THash<U> hash;
        return hash(t);
    }

    void swap(THash<NUniqContainer::TRefWithIndex<T>>&) {
        // do NOT swap container references
    }

    const TVector<T>& vector_ref;
};

template <class T>
struct TEqualTo<NUniqContainer::TRefWithIndex<T>> {
    using is_transparent = void;
    explicit TEqualTo(const TVector<T>& ref) : vector_ref(ref) {}

    inline bool operator()(const NUniqContainer::TRefWithIndex<T>& r1, const NUniqContainer::TRefWithIndex<T>& r2) const {
        return r1.index == r2.index;
    }
    template <class U>
    inline bool operator()(const NUniqContainer::TRefWithIndex<T>& ref, const U& t) const {
        Y_ASSERT(ref.index < vector_ref.size());
        return t == vector_ref[ref.index];
    }

    void swap(TEqualTo<NUniqContainer::TRefWithIndex<T>>&) {
        // do NOT swap container references
    }

    const TVector<T>& vector_ref;
};

template <class T, bool Enable = (std::is_integral<T>::value || std::is_pointer<T>::value || std::is_enum_v<T>)>
class TUniqVector;

// Intergral types just stored twice
template <class T>
class TUniqVector<T, true> : public TUniqContainerImpl<T, T, 128> {};

template <>
class TUniqVector<TStringBuf, false> : public TUniqContainerImpl<TStringBuf, TStringBuf, 32> {};

template <>
class TUniqVector<TString, false> : public TUniqContainerImpl<TString, NUniqContainer::TRefWithIndex<TString>, 32> {};

template <>
class TUniqVector<std::string, false> : public TUniqContainerImpl<std::string, NUniqContainer::TRefWithIndex<std::string>, 32> {};

// Non-Intergral types stored via TRefWrapper
// Note T should be hashable and equaly comparable
template <class T>
class TUniqDeque: public TUniqContainerImpl<T, NUniqContainer::TRefWrapper<T>, 2, TDeque<T>> {};


// try to avoid creating new objects
template <class V>
void AddTo(const TSimpleSharedPtr<V>& from, TSimpleSharedPtr<V>& to) {
    if (!from || from->empty() || from == to) {
        return;
    }
    if (!to) {
        to = from;
        return;
    }
    if (from->IsSubsetOf(*to)) {
        return;
    }
    // now we really have to create something new
    if (to.RefCount() > 1) // btw this is not thread-safe
        to = MakeSimpleShared<V>(*to);
    from->AddTo(*to);
}

template <class T, class V>
void AddTo(const T& what, TSimpleSharedPtr<V>& to) {
    if (to && to->has(what)) {
        return;
    }
    if (!to) {
        to = MakeSimpleShared<V>();
    } else if (to.RefCount() > 1) { // btw this is not thread-safe
        to = MakeSimpleShared<V>(*to);
    }
    to->Push(what);
}

template <class V>
void AddTo(const THolder<V>& from, THolder<V>& to) {
    if (!from || from->empty() || from == to) {
        return;
    }
    if (!to) {
        to = MakeHolder<V>(*from);
        return;
    }
    from->AddTo(*to);
}

template <class T, class V>
inline void AddTo(const T& what, THolder<V>& to) {
    if (!to) {
        to = MakeHolder<V>();
    }
    to->Push(what);
}

template<typename T, bool E>
struct TSerializer<TUniqVector<T, E>> {
    static inline void Save(IOutputStream* rh, const TUniqVector<T, E>& v) {
        TSerializer<TVector<T>>::Save(rh, v.Data());
    }

    static inline void Load(IInputStream* rh, TUniqVector<T, E>& v) {
        TVector<T> temp;
        TSerializer<TVector<T>>::Load(rh, temp);
        for (const T& elem : temp) {
            v.Push(std::move(elem));
        }
    }
};
