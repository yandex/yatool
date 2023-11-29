LIBRARY()

PEERDIR(
    devtools/local_cache/common/logger-utils
    devtools/local_cache/psingleton/service
    devtools/local_cache/ac/dbbe
    devtools/local_cache/ac/proto
    library/cpp/config
    library/cpp/logger/global
)

SRCS(
    config.cpp
    service.cpp
)

GENERATE_ENUM_SERIALIZATION(config.h)

END()
