LIBRARY()

SRCS(
    config.cpp
    logger.cpp
    logger_filter.cpp
    process.cpp
    pgroup.cpp
)

PEERDIR(
    library/cpp/logger
    library/cpp/logger/global
    library/cpp/resource
    devtools/libs/yaplatform
    devtools/ya/app_config/lib
)

END()

RECURSE_FOR_TESTS(
    ut
)

RECURSE(
    edl
    start_stager
)
