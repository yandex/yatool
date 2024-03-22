PY23_LIBRARY()

PY_SRCS(
    lib.pyx
)

PEERDIR(
    devtools/executor/proc_util
)

END()
