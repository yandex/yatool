#pragma once

#include <util/generic/cast.h>
#include <util/generic/hash.h>

enum class TNodeId: ui32 {
    Invalid = 0,
    MinValid = 1
};
constexpr TNodeId& operator++ (TNodeId& id) noexcept {
    id = TNodeId{ToUnderlying(id) + 1};
    return id;
}
constexpr TNodeId operator++ (TNodeId& id, int) noexcept {
    return std::exchange(id, TNodeId{ToUnderlying(id) + 1});
}
constexpr TNodeId& operator-- (TNodeId& id) noexcept {
    id = TNodeId{ToUnderlying(id) - 1};
    return id;
}
constexpr TNodeId operator-- (TNodeId& id, int) noexcept {
    return std::exchange(id, TNodeId{ToUnderlying(id) - 1});
}

// TODO: Investigate if using default hash for enum affects performance. Switching
// node id hash function from default for ui32 to default for enum breaks at least one
// test which relies on iteration order over hash map:
// https://a.yandex-team.ru/arcadia/devtools/ymake/compact_graph/iter_ut.cpp?rev=r10491100#L684
// Most likely that it's badly written test but it is kept untouched during turning TNodeId
// into strongly typed enum.
template<>
struct hash<TNodeId> {
    size_t operator() (const TNodeId& k) const noexcept {
        return hash<ui32>{}(ToUnderlying(k));
    }
};
