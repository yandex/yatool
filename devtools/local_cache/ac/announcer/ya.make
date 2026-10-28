LIBRARY()

SRCS(announcer.cpp)

PEERDIR(
    devtools/local_cache/ac/proto
    devtools/shared_cache/proto
    contrib/libs/grpc
    library/cpp/logger/global
)

END()

RECURSE_FOR_TESTS(ut)
