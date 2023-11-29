LIBRARY()

SRCS(
    fallback_logger.cpp
    simple_stats.cpp
)

PEERDIR(
    library/cpp/logger
    library/cpp/deprecated/atomic
)

END()
