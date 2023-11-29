LIBRARY()

SRCS(
    proc_util.cpp
    server.cpp
)

PEERDIR(
    devtools/executor/net
    devtools/executor/proc_info
    devtools/executor/proto
    library/cpp/sighandler
    library/cpp/deprecated/atomic
)

END()
