PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.loggers.file_log
    __init__.py
)

PEERDIR(
    contrib/python/psutil
    devtools/ya/core/sec
    devtools/ya/exts
)

END()

RECURSE(
    tests
    tests_ext
)
