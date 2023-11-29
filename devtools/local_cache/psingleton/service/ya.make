LIBRARY(psingleton)

PEERDIR(
    devtools/local_cache/psingleton/proto
    contrib/libs/grpc/src/proto/grpc/health/v1
    library/cpp/logger/global
)

SRCS(
    service.cpp
)

END()
