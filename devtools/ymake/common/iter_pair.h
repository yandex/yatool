#pragma once

#include <type_traits>
#include <utility>
#include <iterator>

namespace NIterPairDetail {
    /// Trait detecting pair of comparable iterators
    template <typename T1, typename T2>
    auto IsIterPairImpl(int) // (int) to win overload resoltion over (...)
        -> decltype(
            std::declval<T1&>() != std::declval<T2&>(),        // (begin != end)
            ++std::declval<decltype(std::declval<T1&>()) &>(), // ++begin
            *std::declval<T1&>(),                              // *begin
            std::true_type{});

    template <typename T1, typename T2>
    std::false_type IsIterPairImpl(...);

    /// Trait to get element type from iterator type
    template <class TIter>
    struct TIterValueImpl {
        typedef typename std::remove_cvref<decltype(*(std::declval<TIter&>()))>::type type;
    };
}

/// Value type from Iterator type
template <class TIter>
using TIterValue = typename NIterPairDetail::TIterValueImpl<TIter>::type;

/// Check that types constitute a compatible pair of iterators
template <typename T1, typename T2>
using IsIterPair = decltype(NIterPairDetail::IsIterPairImpl<T1, T2>(0));

/// Create iterator pair from the pair of iterators
template <typename T1, typename T2, typename = std::enable_if_t<IsIterPair<T1, T2>::value>>
auto MakeIterPair(T1 b, T2 e) {
    return std::make_pair(b, e);
}

/// Create iterator pair from the single iterator
/// usable for conditional skip `MakeIterPair(end, end)`
template <typename T, typename = std::enable_if_t<IsIterPair<T, T>::value>>
auto MakeIterSkipPair(T it) {
    return std::make_pair(it, it);
}

/// Create iterator pair from the single iterator
/// usable for conditional single-element access
template <typename T, typename = std::enable_if_t<IsIterPair<T, T>::value>>
auto MakeIterValuePair(T it) {
    T next = it;
    return std::make_pair(it, ++next);
}

/// Create iterator pair from the collection
template <typename T, typename = std::enable_if_t<!IsIterPair<T, T>::value>>
auto MakeIterPair(const T& col) {
    return MakeIterPair(std::begin(col), std::end(col));
}
