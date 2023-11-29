LIBRARY()

SRCS(
    path_resolver.cpp
    resolve_cache.cpp
    resolve_ctx.cpp
)

PEERDIR(
    devtools/ymake/options
    devtools/ymake/common
    devtools/ymake/compact_graph
)

END()

RECURSE(
    ut
)
