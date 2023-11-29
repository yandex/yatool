PY23_LIBRARY()

PY_SRCS(
    executor.pyx
)

IF (OS_LINUX)
    PEERDIR(
        # Don't use contrib/python/python-prctl - it won't build on lucid
        library/python/prctl
    )
ENDIF()

PEERDIR(
    contrib/python/psutil
    devtools/executor/lib
    devtools/executor/proto
    devtools/ya/exts
)

END()
