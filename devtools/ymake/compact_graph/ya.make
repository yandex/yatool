LIBRARY()

PEERDIR(
    devtools/ymake/common
    devtools/ymake/diag
    devtools/ymake/symbols
    library/cpp/packedtypes
    library/cpp/remmap
)

SRCS(
    bfs_iter.cpp
    graph.cpp
    dep_graph.cpp
    dep_types.cpp
    iter.cpp
    iter_direct_peerdir.cpp
    iter_starts_ctx.cpp
    legacy_iterator.cpp
    nodes_queue.cpp
    peer_collector.cpp
    query.cpp
    loops.cpp
)

GENERATE_ENUM_SERIALIZATION(dep_types.h)

END()
