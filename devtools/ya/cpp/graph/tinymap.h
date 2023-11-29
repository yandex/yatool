#pragma once

#include <devtools/ya/cpp/lib/edl/common/loaders.h>
#include <devtools/ya/cpp/lib/edl/common/export_helpers.h>

#include <util/generic/scope.h>
#include <util/generic/yexception.h>

#include <functional>
#include <memory>
#include <utility>

namespace NYa::NGraph {

    // This is a map with O(N) complexity almost for all operations but it may be the case when O(N) less than O(log N) or even O(1). Measure it!
    // It is very compact and fast if element count is small.

    // Important note:
    //   value_type.first is not const. It allows to call pair's move-constructor and move-assignment on map resize or after element erasing.
    //   But, please, never change the first value member if you want keys to be unique inside the map.

    template <class Key, class T>
    class TTinyMap {
    public:
        using key_type = Key;
        using mapped_type = T;
        using value_type = std::pair<key_type, mapped_type>;
        using allocator_type = std::allocator<value_type>;
        using key_equal = std::equal_to<key_type>;
        using reference = value_type&;
        using const_reference = value_type&;
        using pointer = typename std::allocator_traits<allocator_type>::pointer;
        using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;
        using iterator = pointer;
        using const_iterator = const_pointer;
        using size_type = size_t;
        using difference_type = ptrdiff_t;

        TTinyMap()
            : Data_{}
            , Capacity_{}
            , Size_{}
        {
        }

        TTinyMap(size_type capacity)
            : TTinyMap{}
        {
            Y_ENSURE(capacity > 0 && capacity <= MaxCapacity);
            Extend(capacity);
        }

        template <class TIter>
        TTinyMap(TIter first, TIter last)
            : TTinyMap{}
        {
            size_type n = std::distance(first, last);
            if (n > 0) {
                Extend(n);
                for (; first != last; ++first) {
                    AddValue(*first);
                }
            }
        }

        TTinyMap(const TTinyMap& other)
            : TTinyMap{}
        {
            if (other.Capacity_ > 0) {
                Extend(other.Capacity_);
                const_pointer src = other.Data_;
                const_pointer end = other.Data_ + other.Size_;
                pointer dest = Data_;
                for (; src != end; ++src, ++dest) {
                    std::construct_at(dest, *src);
                    ++Size_;
                }
            }
        }

        TTinyMap(TTinyMap&& other) noexcept {
            Data_ = other.Data_;
            Capacity_ = other.Capacity_;
            Size_ = other.Size_;
            other.Data_ = nullptr;
            other.Capacity_ = other.Size_ = 0;
        }

        TTinyMap(std::initializer_list<value_type> init)
            : TTinyMap{}
        {
            if (init.size() > 0) {
                Extend(init.size());
                for(auto&& v : init) {
                    AddValue(v);
                }
            }
        }

        ~TTinyMap() {
            clear();
        }

        TTinyMap& operator=(const TTinyMap& other) {
            if (&other != this) {
                TTinyMap newVal{other};
                *this = std::move(newVal);
            }
            return *this;
        }

        TTinyMap& operator=(TTinyMap&& other) noexcept {
            if (&other != this) {
                clear();
                Data_ = other.Data_;
                Capacity_ = other.Capacity_;
                Size_ = other.Size_;
                other.Data_ = nullptr;
                other.Capacity_ = other.Size_ = 0;
            }
            return *this;
        }

        size_type size() const noexcept {
            return Size_;
        }

        size_type capacity() const noexcept {
            return Capacity_;
        }

        size_type max_size() const noexcept {
            return MaxCapacity;
        }

        bool empty() const noexcept {
            return !Size_;
        }

        bool contains(const key_type& key) {
            return FindItem(key);
        }

        size_type count(const key_type& key) {
            return FindItem(key) ? 1u : 0u;
        }

        iterator find(const key_type& key) {
            if (pointer p = FindItem(key)) {
                return MakeIter(p);
            }
            return end();
        }

        const_iterator find(const key_type& key) const {
            if (const_pointer p = FindItem(key)) {
                return MakeIter(p);
            }
            return end();
        }

        iterator begin() noexcept {
            return MakeIter(Data_);
        }

        const_iterator begin() const noexcept {
            return MakeIter(Data_);
        }

        const_iterator cbegin() const noexcept {
            return begin();
        }

        iterator end() noexcept {
            return MakeIter(Data_ + Size_);
        }

        const_iterator end() const noexcept {
            return MakeIter(Data_ + Size_);
        }

        const_iterator cend() const noexcept {
            return end();
        }

        mapped_type& operator[](const key_type& key) {
            if (pointer p = FindItem(key)) {
                return p->second;
            }
            return AddEmptyValue(key);
        }

        mapped_type& at(const key_type& key) {
            if (pointer p = FindItem(key)) {
                return p->second;
            }
            ythrow yexception() << "Key not found";
        }

        const mapped_type& at(const key_type& key) const {
            if (const_pointer p = FindItem(key)) {
                return p->second;
            }
            ythrow yexception() << "Key not found";
        }

        std::pair<iterator, bool> insert(const value_type& value) {
            if (pointer p = FindItem(value.first)) {
                return std::make_pair(MakeIter(p), false);
            }
            return std::make_pair(AddValue(value), true);
        }

        std::pair<iterator, bool> insert(value_type&& value) {
            if (pointer p = FindItem(value.first)) {
                return std::make_pair(MakeIter(p), false);
            }
            return std::make_pair(AddValue(std::move(value)), true);
        }

        template <class TIter>
        void insert(TIter first, TIter last) {
            for (; first != last; ++first) {
                if (!FindItem(first->first)) {
                    AddValue(*first);
                }
            }
        }

        void insert(std::initializer_list<value_type> ilist) {
            for (auto&& v : ilist) {
                if (!FindItem(v.first)) {
                    AddValue(v);
                }
            }
        }

        template <class M>
        std::pair<iterator, bool> insert_or_assign(const key_type& key, M&& value) {
            if (pointer p = FindItem(key)) {
                p->second = std::forward<M>(value);
                return std::make_pair(MakeIter(p), false);
            }
            iterator p = AddValue(std::make_pair(key, std::forward<M>(value)));
            return std::make_pair(p, true);
        }

        template <class... Args>
        std::pair<iterator,bool> emplace(Args&&... args) {
            return insert(value_type(std::forward<Args>(args)...));
        }

        template <class... Args>
        std::pair<iterator,bool> try_emplace(const key_type& key, Args&&... args) {
            if (pointer p = FindItem(key)) {
                return std::make_pair(MakeIter(p), false);
            }
            iterator p = AddValue(std::make_pair(key, mapped_type(std::forward<Args>(args)...)));
            return std::make_pair(p, true);
        }

        iterator erase(iterator pos) {
            Y_ASSERT(pos >= begin() && pos < end());
            return erase(pos, pos + 1);
        }

        iterator erase(const_iterator pos) {
            Y_ASSERT(pos >= cbegin() && pos < cend());
            return erase(pos, pos + 1);
        }

        iterator erase(const_iterator first, const_iterator last) {
            Y_ASSERT(first >= cbegin() && first <= cend());
            Y_ASSERT(last >= first && last <= cend());
            EraseElements(first, last);
            return begin() + (first - cbegin());
        }

        size_type erase(const key_type& key) {
            if (pointer p = FindItem(key)) {
                erase(p);
                return 1;
            }
            return 0;
        }

        void swap(TTinyMap& other) noexcept {
            std::swap(Data_, other.Data_);
            std::swap(Capacity_, other.Capacity_);
            std::swap(Size_, other.Size_);
        }

        void clear() noexcept {
            std::destroy(Data_, Data_ + Size_);
            if (Data_) {
                std::allocator_traits<allocator_type>::deallocate(Alloc_, Data_, Capacity_);
                Data_ = nullptr;
            }
            Capacity_ = Size_ = 0;
        }

    private:
        using TSize = uint16_t;
        static constexpr size_type MaxCapacity = Max<TSize>();

    private:
        mapped_type& AddEmptyValue(const key_type& key) {
            return AddValue(key, mapped_type{})->second;
        }

        template <class... Args>
        iterator AddValue(Args&&... args) {
            if (Size_ == Capacity_) {
                size_type capacity = Capacity_ ? Capacity_ * 2 : 2;
                Extend(capacity);
            }
            pointer p = Data_ + Size_;
            std::construct_at(p, std::forward<Args>(args)...);
            ++Size_;
            return MakeIter(p);
        }

        pointer FindItem(const key_type& key) const {
            for (pointer p = Data_; p != Data_ + Size_; ++p) {
                if (KeyEqual_(key, p->first)) {
                    return p;
                }
            }
            return nullptr;
        }

        void Extend(size_type capacity) {
            TSize newCap = Min(MaxCapacity, capacity);
            Y_ENSURE(newCap > Capacity_);
            pointer newData = Alloc_.allocate(newCap);
            TSize newSize = 0;
            Y_DEFER {
                if (newData) {
                    std::destroy(newData, newData + newSize);
                    std::allocator_traits<allocator_type>::deallocate(Alloc_, newData, newCap);
                }
            };
            pointer src = Data_;
            pointer dest = newData;
            for (; src != Data_ + Size_; ++src, ++dest) {
                if constexpr (std::is_nothrow_move_constructible_v<value_type>) {
                    std::construct_at(dest, std::move(*src));
                } else {
                    std::construct_at(dest, *src);
                }
                ++newSize;
            }
            Y_ASSERT(newSize == Size_);
            std::swap(Data_, newData);
            std::swap(Capacity_, newCap);
        }

        void EraseElements(const_iterator first, const_iterator last) {
            ptrdiff_t n = last - first;
            if (n == 0) {
                return;
            }
            // Map is not a vector so we don't have to preserve element order.
            // Choose the way with the minimal moving element count: move at most n elements.
            ptrdiff_t tail = Min(n, cend() - last);
            pointer from = Data_ + Size_ - tail;
            pointer to = Data_+ (first - cbegin());
            std::move(from, Data_ + Size_, to);
            std::destroy(Data_ + Size_ - n, Data_ + Size_);
            Size_ -= n;
        }

        iterator MakeIter(pointer p) noexcept {
            return iterator(p);
        }

        const_iterator MakeIter(const_pointer p) const noexcept {
            return iterator(p);
        }

    private:
        inline static allocator_type Alloc_{};
        inline static key_equal KeyEqual_{};
        pointer Data_;
        TSize Capacity_;
        TSize Size_;
    };

    template <class Key, class T>
    bool operator==(const TTinyMap<Key, T>& lhs, const TTinyMap<Key, T>& rhs) {
        using TValueType = typename TTinyMap<Key, T>::value_type;
        if (lhs.size() != rhs.size()) {
            return false;
        }
        THashMap<Key, T> lhsItems{lhs.begin(), lhs.end()};
        for (const TValueType& r : rhs) {
            auto p = lhsItems.FindPtr(r.first);
            if (!p || *p != r.second) {
                return false;
            }
        }
        return true;
    }

    template <class Key, class T>
    bool operator!=(const TTinyMap<Key, T>& lhs, const TTinyMap<Key, T>& rhs) {
        return !(lhs == rhs);
    }
}

namespace NYa::NEdl {
    template <class K, class V>
    class TLoader<NYa::NGraph::TTinyMap<K, V>> : public TLoaderForRef<NYa::NGraph::TTinyMap<K, V>> {
        using TLoaderForRef<NYa::NGraph::TTinyMap<K, V>>::TLoaderForRef;

        inline void EnsureMap() override {
        }

        TLoaderPtr AddMapValue(TStringBuf key) override {
            return GetLoader(this->ValueRef_[key]);
        }
    };

    template <class E, class K, class V>
    struct TExportHelper<E, NYa::NGraph::TTinyMap<K, V>> {
        static void Export(E&& e, const NYa::NGraph::TTinyMap<K, V>& map) {
            ExportMap(std::forward<E>(e), map);
        }
    };
};
