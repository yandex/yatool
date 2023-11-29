LIBRARY()

SRCS(
    dbbe.cpp
    integrity.cpp
    running_poller.cpp
)

PEERDIR(
    devtools/local_cache/common/dbbe-running-procs
    devtools/local_cache/common/logger-utils
    devtools/local_cache/ac/db
    library/cpp/logger/global
)

END()

# RECURSE_FOR_TESTS(fat)
