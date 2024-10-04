#pragma once

#include "nodeid.h"
#include "nodeids_range.h"

#include <util/generic/deque.h>
#include <util/ysaveload.h>

#include <concepts>

template<std::default_initializable T, template<typename...> typename TContainer = TDeque>
class TNodesData {
public:
    using value_type = typename TContainer<T>::value_type;
    using iterator = typename TContainer<T>::iterator;
    using const_iterator = typename TContainer<T>::const_iterator;

    TNodesData() {Storage_.emplace_back();}

    const T& operator[] (TNodeId id) const noexcept {
        return Storage_[ToUnderlying(id)];
    }

    T& operator[] (TNodeId id) noexcept {
        return Storage_[ToUnderlying(id)];
    }

    const T& back() const noexcept {
        return Storage_.back();
    }

    T& back() noexcept {
        return Storage_.back();
    }

    bool contains(TNodeId id) const noexcept {return ToUnderlying(id) < Storage_.size();}

    TValidNodeIds ValidIds() const noexcept {return {MaxNodeId()};}

    iterator begin() {return Storage_.begin();}
    iterator end() {return Storage_.end();}

    const_iterator begin() const {return Storage_.begin();}
    const_iterator end() const {return Storage_.end();}

    const_iterator cbegin() const {return Storage_.begin();}
    const_iterator cend() const {return Storage_.end();}

    size_t size() const noexcept {
        return Storage_.size();
    }

    void SetMaxNodeIdByResize(TNodeId maxId) {
        Storage_.resize(ToUnderlying(maxId) + 1);
    }

    TNodeId MaxNodeId() const noexcept {
        return static_cast<TNodeId>(Storage_.size() - 1);
    }

    void clear() {
        Storage_.clear();
        Storage_.emplace_back();
    }

    TNodeId push_back(const T& val) {
        const auto res = static_cast<TNodeId>(Storage_.size());
        Storage_.push_back(val);
        return res;
    }

    TNodeId push_back(T&& val) {
        const auto res = static_cast<TNodeId>(Storage_.size());
        Storage_.push_back(std::move(val));
        return res;
    }

    template<typename... A>
    TNodeId emplace_back(A&&... a) {
        const auto res = static_cast<TNodeId>(Storage_.size());
        Storage_.emplace_back(std::forward<A>(a)...);
        return res;
    }

    template<std::predicate<value_type> Pred>
    void Compact(Pred&& pred) {
        Storage_.erase(
            std::remove_if(std::next(Storage_.begin()), Storage_.end(), std::forward<Pred>(pred)),
            Storage_.end()
        );
    }

    Y_SAVELOAD_DEFINE(Storage_);

private:
    TContainer<T> Storage_;
};
