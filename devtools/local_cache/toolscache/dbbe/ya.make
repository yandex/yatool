LIBRARY()

SRCS(
    dbbe.cpp
    gc_fs.cpp
    integrity.cpp
    running_poller.cpp
)

PEERDIR(
    devtools/local_cache/common/dbbe-running-procs
    devtools/local_cache/toolscache/db
    library/cpp/logger/global
    library/cpp/threading/future
)

END()

RECURSE_FOR_TESTS(fat)
