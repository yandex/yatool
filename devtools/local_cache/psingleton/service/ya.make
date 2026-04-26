LIBRARY(psingleton)

PEERDIR(
    devtools/local_cache/psingleton/proto
    contrib/proto/grpc/grpc/health/v1
    library/cpp/logger/global
)

SRCS(
    service.cpp
)

END()
