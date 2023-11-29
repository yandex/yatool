LIBRARY()

PEERDIR(
    devtools/local_cache/common/fs-utils
    devtools/local_cache/toolscache/proto
    library/cpp/threading/future
)

SRCS(
    fs_ops.cpp
)

END()

RECURSE_FOR_TESTS(du)

RECURSE_FOR_TESTS(ut)
