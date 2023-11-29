PROGRAM(ya-tc)

SRCS(
    client.cpp
    config.cpp
    server.cpp
    server-sys.cpp
)

PEERDIR(
    devtools/local_cache/common/logger-utils
    devtools/local_cache/common/server-utils
    devtools/local_cache/ac/service
    devtools/local_cache/toolscache/service
    devtools/local_cache/psingleton
    devtools/local_cache/psingleton/server
    library/cpp/getopt
    library/cpp/config
    util/draft
)

GENERATE_ENUM_SERIALIZATION(config.h)

RESOURCE(
    tc.ini tc/ini-file
    ac.ini ac/ini-file
)

END()
