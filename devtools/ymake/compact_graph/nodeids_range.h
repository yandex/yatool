#pragma once

#include "nodeid.h"

namespace NDetail {

class TNodeIdsIterator {
public:
    using value_type = TNodeId;
    using difference_type = std::ptrdiff_t;

    constexpr TNodeIdsIterator() noexcept = default;
    constexpr TNodeIdsIterator(std::underlying_type_t<TNodeId> state) noexcept: State_{state} {}

    constexpr std::strong_ordering operator<=> (const TNodeIdsIterator&) const noexcept = default;

    constexpr value_type operator[] (difference_type off) const noexcept {return static_cast<TNodeId>(State_ + off);}

    constexpr TNodeIdsIterator& operator-= (difference_type off) noexcept {State_ -= off; return *this;}
    constexpr difference_type operator- (const TNodeIdsIterator& rhs) const noexcept {return State_ - rhs.State_;}
    constexpr TNodeIdsIterator operator- (difference_type off) const noexcept {return {static_cast<ui32>(State_ - off)};}

    constexpr TNodeIdsIterator& operator+= (difference_type off) noexcept {State_ += off; return *this;}
    constexpr TNodeIdsIterator operator+ (difference_type off) const noexcept {return {static_cast<ui32>(State_ + off)};}

    constexpr value_type operator* () const noexcept {return static_cast<TNodeId>(State_);}

    constexpr TNodeIdsIterator& operator++ () noexcept {++State_; return *this;}
    constexpr TNodeIdsIterator operator++ (int) noexcept {return {State_++};}

    constexpr TNodeIdsIterator& operator-- () noexcept {--State_; return *this;}
    constexpr TNodeIdsIterator operator-- (int) noexcept {return {State_--};}

private:
    std::underlying_type_t<TNodeId> State_ = 0;
};

TNodeIdsIterator operator+ (TNodeIdsIterator::difference_type, const TNodeIdsIterator&) noexcept;

class TNodeIdsRangeBase {
public:
    using value_type = TNodeId;

    constexpr TNodeIdsRangeBase() noexcept = default;
    constexpr TNodeIdsRangeBase(TNodeId maxNodeId) noexcept: End_{ToUnderlying(maxNodeId) + 1} {}

    constexpr TNodeIdsIterator end() const noexcept {
        return End_;
    }

private:
    TNodeIdsIterator End_ = {ToUnderlying(TNodeId::MinValid)};
};

template<TNodeId Start>
class TNodeIdsRange: public NDetail::TNodeIdsRangeBase {
public:
    constexpr NDetail::TNodeIdsIterator begin() const noexcept {return {ToUnderlying(Start)};}

    constexpr size_t size() const noexcept {return end() - begin();}
};

}

using TValidNodeIds = NDetail::TNodeIdsRange<TNodeId::MinValid>;
using TAllNodeIds = NDetail::TNodeIdsRange<TNodeId::Invalid>;
