#pragma once

#include "nodeid.h"

namespace NDetail {

class TNodeIdsIterator {
public:
    using value_type = TNodeId;
    using difference_type = std::ptrdiff_t;

    constexpr TNodeIdsIterator() noexcept = default;
    constexpr TNodeIdsIterator(ui32 state) noexcept: State_{state} {}

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
    ui32 State_ = 0;
};

TNodeIdsIterator operator+ (TNodeIdsIterator::difference_type, const TNodeIdsIterator&) noexcept;

class TNodeIdsRangeBase {
public:
    using value_type = TNodeId;

    constexpr TNodeIdsRangeBase() noexcept = default;
    constexpr TNodeIdsRangeBase(TNodeId maxNodeId) noexcept: MaxIdRepr_(ToUnderlying(maxNodeId)) {}

    constexpr TNodeIdsIterator end() const noexcept {
        return {MaxIdRepr_ + 1};
    }

protected:
    constexpr size_t SizeFrom(TNodeId start) const noexcept {
        return MaxIdRepr_ - ToUnderlying(start) + 1;
    }

private:
    ui32 MaxIdRepr_ = 0;
};

template<TNodeId Start>
class TNodeIdsRange: public NDetail::TNodeIdsRangeBase {
public:
    constexpr NDetail::TNodeIdsIterator begin() const noexcept {return {ToUnderlying(Start)};}

    constexpr size_t size() const noexcept {return SizeFrom(Start);}
};

}

using TValidNodeIds = NDetail::TNodeIdsRange<TNodeId::MinValid>;
using TAllNodeIds = NDetail::TNodeIdsRange<TNodeId::Invalid>;
