LIBRARY()

SRCS(
    graph.cpp
    intern_string.cpp
)

GENERATE_ENUM_SERIALIZATION(graph.h)

PEERDIR(
    devtools/ya/cpp/lib/edl/common
    devtools/ya/cpp/lib/edl/json
    library/cpp/digest/md5
    library/cpp/json/common
    library/cpp/json/fast_sax
    library/cpp/json/writer
)

END()

RECURSE_FOR_TESTS(
    ut
)
