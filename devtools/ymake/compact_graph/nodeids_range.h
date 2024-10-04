#pragma once

#include "nodeid.h"

namespace NDetail {

class TNodeIdsIterator {
public:
    using value_type = TNodeId;
    using difference_type = std::ptrdiff_t;

    constexpr TNodeIdsIterator() noexcept = default;
    constexpr TNodeIdsIterator(ui32 state) noexcept: State{state} {}

    constexpr std::strong_ordering operator<=> (const TNodeIdsIterator&) const noexcept = default;

    constexpr value_type operator[] (difference_type off) const noexcept {return static_cast<TNodeId>(State + off);}

    constexpr TNodeIdsIterator& operator-= (difference_type off) noexcept {State -= off; return *this;}
    constexpr difference_type operator- (const TNodeIdsIterator& rhs) const noexcept {return State - rhs.State;}
    constexpr TNodeIdsIterator operator- (difference_type off) const noexcept {return {static_cast<ui32>(State - off)};}

    constexpr TNodeIdsIterator& operator+= (difference_type off) noexcept {State += off; return *this;}
    constexpr TNodeIdsIterator operator+ (difference_type off) const noexcept {return {static_cast<ui32>(State + off)};}

    constexpr value_type operator* () const noexcept {return static_cast<TNodeId>(State);}

    constexpr TNodeIdsIterator& operator++ () noexcept {++State; return *this;}
    constexpr TNodeIdsIterator operator++ (int) noexcept {return {State++};}

    constexpr TNodeIdsIterator& operator-- () noexcept {--State; return *this;}
    constexpr TNodeIdsIterator operator-- (int) noexcept {return {State--};}

private:
    ui32 State = 0;
};

TNodeIdsIterator operator+ (TNodeIdsIterator::difference_type, const TNodeIdsIterator&) noexcept;

class TNodeIdsRangeBase {
public:
    using value_type = TNodeId;

    constexpr TNodeIdsRangeBase() noexcept = default;
    constexpr TNodeIdsRangeBase(TNodeId maxNodeId) noexcept: MaxIdRepr(ToUnderlying(maxNodeId)) {}

    constexpr TNodeIdsIterator end() const noexcept {
        return {MaxIdRepr + 1};
    }

private:
    ui32 MaxIdRepr = 0;
};

}

class TValidNodeIds: public NDetail::TNodeIdsRangeBase {
public:
    constexpr NDetail::TNodeIdsIterator begin() const noexcept {return {ToUnderlying(TNodeId::MinValid)};}
};

class TAllNodeIds: public NDetail::TNodeIdsRangeBase {
public:
    constexpr NDetail::TNodeIdsIterator begin() const noexcept {return {ToUnderlying(TNodeId::Invalid)};}
};
