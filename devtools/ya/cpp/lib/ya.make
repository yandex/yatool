LIBRARY()

SRCS(
    config.cpp
    logger.cpp
    logger_filter.cpp
    process.cpp
)

PEERDIR(
    library/cpp/logger
    library/cpp/logger/global
    library/cpp/resource
    devtools/libs/yaplatform
)

END()

RECURSE_FOR_TESTS(
    ut
)

RECURSE(
    edl
    json_sax
    start_stager
)
