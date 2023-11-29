LIBRARY()

PEERDIR(
    devtools/local_cache/psingleton/service
    devtools/local_cache/toolscache/dbbe
    devtools/local_cache/toolscache/proto
    library/cpp/config
    library/cpp/logger/global
)

SRCS(
    config.cpp
    service.cpp
)

GENERATE_ENUM_SERIALIZATION(config.h)

END()
