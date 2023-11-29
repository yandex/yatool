LIBRARY(psingleton)

PEERDIR(
    devtools/local_cache/common/logger-utils
    library/cpp/logger/global
    contrib/libs/grpc
    contrib/libs/grpc/src/proto/grpc/health/v1
)

SRCS(
    systemptr-sys.cpp
    systemptr.cpp
    systemptr.rl6
)

END()

RECURSE(
    proto
    python
    server
    service
)

RECURSE_FOR_TESTS(
    tests
    ut
)
