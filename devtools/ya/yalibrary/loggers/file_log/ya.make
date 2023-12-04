PY23_LIBRARY()

STYLE_PYTHON()

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
