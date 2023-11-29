#pragma once

#include "iter_pair.h"

#include <functional>
#include <tuple>
#include <utility>

///
/// Iterable tuple provides the way to iterate through a set of individual elements, iterator pairs
/// and nested iterable tuples all known at compile time.
///
/// In order to use this, one should instantiate one of runners with the functional object handling
/// single element and call Run() function over a tuple argument.
///
namespace NIterTupleDetail {
    template <std::size_t I = 0, typename TRunner, typename... Tp>
    typename std::enable_if<I == sizeof...(Tp), bool>::type CheckAny(std::tuple<Tp...>&, TRunner) {
        return false;
    }

    /// Recursive check through tuple elements until first match
    template <std::size_t I = 0, typename TRunner, typename... Tp>
    typename std::enable_if<I<sizeof...(Tp), bool>::type CheckAny(std::tuple<Tp...>& t, TRunner runner) {
        if (!runner(std::get<I>(t))) {
            return CheckAny<I + 1, TRunner, Tp...>(t, runner);
        } else {
            return true;
        }
    }

    template <std::size_t I = 0, typename TRunner, typename... Tp>
    typename std::enable_if<I == sizeof...(Tp), void>::type ForAll(std::tuple<Tp...>&, TRunner) {
    }

    /// Recursive run through tuple elements
    template <std::size_t I = 0, typename TRunner, typename... Tp>
    typename std::enable_if<I<sizeof...(Tp), void>::type ForAll(std::tuple<Tp...>& t, TRunner runner) {
        runner(std::get<I>(t));
        ForAll<I + 1, TRunner, Tp...>(t, runner);
    }

    /// Base checking runner implements processing of individual elements, iterator pairs and nested tuples
    template <typename TValue, typename TFunc>
    struct TIterTupleCheckerImpl {
        TFunc& Fun;

        TIterTupleCheckerImpl(TFunc& fun)
            : Fun(fun)
        {
        }

        bool operator()(const TValue& val) {
            return Fun(val);
        }

        template <class TIter,
                  typename = std::enable_if_t<IsIterPair<TIter, TIter>::value>,
                  typename = std::enable_if_t<std::is_same<TValue, TIterValue<TIter>>::value
                                           || std::is_constructible<TValue, TIterValue<TIter>>::value>>
        bool operator()(std::pair<TIter, TIter> itp) {
            for (auto it = itp.first; it != itp.second; ++it) {
                if (std::is_same<TValue, TIterValue<TIter>>::value) {
                    if (Fun(*it)) {
                        return true;
                    }
                } else {
                    if (Fun(TValue(*it))) {
                        return true;
                    }
                }
            }
            return false;
        }

        template <typename... Ts>
        bool operator()(std::tuple<Ts...>& vals) {
            return CheckAny(vals, *this);
        }
    };

    /// @brief Base calling runner implements processing supported items in iterable tuple
    template <typename TValue, typename TFunc>
    struct TIterTupleRunnerImpl {
        TFunc& Fun;

        TIterTupleRunnerImpl(TFunc& fun)
            : Fun(fun)
        {
        }

        /// @brief process individual element
        void operator()(const TValue& val) {
            Fun(val);
        }

        /// @brief process pair of iterators treating the as [begin, end)
        template <class TIter,
                  typename = std::enable_if_t<IsIterPair<TIter, TIter>::value>,
                  typename = std::enable_if_t<std::is_same<TValue, TIterValue<TIter>>::value
                                           || std::is_constructible<TValue, TIterValue<TIter>>::value>>
        void operator()(std::pair<TIter, TIter> itp) {
            for (auto it = itp.first; it != itp.second; ++it) {
                if (std::is_same<TValue, TIterValue<TIter>>::value) {
                    Fun(*it);
                } else {
                    Fun(TValue(*it));
                }
            }
        }

        /// @brief process nested tuple
        template <typename... Ts>
        void operator()(std::tuple<Ts...>& vals) {
            ForAll(vals, *this);
        }
    };
}

///
/// @brief This runner calls the functor supplied over iterable tuple elements until functor returns true.
///
/// Due to compilcated structure of tuple no hint is provided on where match happened. Functor may update
/// its state in order to get a clue if needed.
///
template <typename TValue, typename TFunc = std::function<bool(const TValue&)>>
struct TIterTupleChecker : NIterTupleDetail::TIterTupleCheckerImpl<TValue, TFunc> {
    using TBase = typename NIterTupleDetail::TIterTupleCheckerImpl<TValue, TFunc>;

    /// Instantiate runner with elemental functor matching bool(const TValue&)
    TIterTupleChecker(TFunc& fun)
        : TBase(fun)
    {
    }

    /// @brief call fun on every element until true is returned.
    /// @return false if 'false' was returned for all elements. True otherwise.
    template <typename... Tp>
    bool Run(std::tuple<Tp...>& t) {
        return NIterTupleDetail::CheckAny(t, *static_cast<TBase*>(this));
    }
};

///
/// This runner calls the functor supplied over all iterable tuple elements
///
template <typename TValue, typename TFunc = std::function<void(const TValue&)>>
struct TIterTupleRunner : NIterTupleDetail::TIterTupleRunnerImpl<TValue, TFunc> {
    using TBase = typename NIterTupleDetail::TIterTupleRunnerImpl<TValue, TFunc>;

    /// Instantiate runner with elemental functor matching void(const TValue&)
    TIterTupleRunner(TFunc& fun)
        : TBase(fun)
    {
    }

    /// @brief call fun on every element of iterable tuple
    template <typename... Tp>
    void Run(std::tuple<Tp...>& t) {
        NIterTupleDetail::ForAll(t, *static_cast<TBase*>(this));
    }
};
