#pragma once

#include "graph.h"

#include <util/generic/hash_set.h>
#include <util/generic/queue.h>

template <typename TGraph, typename TPred>
class TBFSQuery {
public:
    using TConstNodeRef = typename TGraph::TConstNodeRef;

    TBFSQuery(const TGraph& graph, const TPred& predicate, TNodeId start)
        : Predicate{predicate}
        , CurDepthItemsRemains{1} {
        Start(graph[start]);
    }

    TBFSQuery(const TGraph& graph, TPred&& predicate, TNodeId start)
        : Predicate{std::move(predicate)}
        , CurDepthItemsRemains{1} {
        Start(graph[start]);
    }

    struct TIterator {
        using iterator_category = std::input_iterator_tag;
        using value_type = TConstNodeRef;
        using pointer = const value_type*;
        using reference = const value_type&;
        using difference_type = std::ptrdiff_t;

        TBFSQuery* Query = nullptr;

        bool operator==(TIterator rhs) const noexcept {
            return Query == rhs.Query;
        }
        bool operator!=(TIterator rhs) const noexcept {
            return Query != rhs.Query;
        }

        TIterator operator++() {
            if (!Query->Next()) {
                Query = nullptr;
            }
            return *this;
        }

        reference operator*() const noexcept {
            return Query->Queue.front();
        }
    };

    TIterator begin() noexcept {
        return {Queue.empty() ? nullptr : this};
    }
    TIterator end() const noexcept {
        return {};
    }

    size_t GetCurrentDepth() const noexcept {
        return CurDepth;
    }

    void SkipCurrentDeps() noexcept {
        Replacement = TNodeId::Invalid;
    }

    void ReplaceCurrent(TNodeId id) noexcept {
        Replacement = id;
    }

private:
    void Start(TConstNodeRef node) {
        if (!node.IsValid()) {
            return;
        }
        Queue.push(node);
    }

    void AddDeps(TConstNodeRef node) {
        Y_ASSERT(node.IsValid());
        for (const auto& dep : node.Edges()) {
            if (dep.To().IsValid() && Predicate(dep)) {
                Queue.push(dep.To());
            }
        }
    }

    bool Next() {
        Y_ASSERT(!Queue.empty());
        Y_ASSERT(Queue.front().IsValid());
        const TNodeId replacement = std::exchange(Replacement, NoReplacement);
        switch (replacement) {
            case NoReplacement:
                AddDeps(Queue.front());
                break;
            case TNodeId::Invalid:
                break;
            default:
                AddDeps(Queue.front().Graph()[replacement]);
                break;
        }
        do {
            Queue.pop();
            if (--CurDepthItemsRemains == 0) {
                ++CurDepth;
                CurDepthItemsRemains = Queue.size();
            }
        } while (!Queue.empty() && !Queue.front().IsValid());
        return !Queue.empty();
    }

private:
    constexpr static TNodeId NoReplacement{std::numeric_limits<std::underlying_type_t<TNodeId>>::max()};

    std::conditional_t<std::is_function_v<TPred>, TPred*, TPred> Predicate;
    TQueue<TConstNodeRef> Queue;
    TNodeId Replacement = NoReplacement;
    size_t CurDepth = 0;
    size_t CurDepthItemsRemains;
};
