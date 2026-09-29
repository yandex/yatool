LIBRARY()

SRCS(
    proc_util.cpp
)

PEERDIR(
    devtools/executor/net
    devtools/executor/proc_info
    library/cpp/deprecated/atomic
)

END()

RECURSE(
    python
)

RECURSE_FOR_TESTS(
    ut
)
