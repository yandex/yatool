PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.loggers.file_log
    __init__.py
)

PEERDIR(
    devtools/ya/core/sec
)

END()

RECURSE(
    tests
    tests_ext
)
