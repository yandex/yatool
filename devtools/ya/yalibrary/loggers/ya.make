PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.loggers
    __init__.py
)

PEERDIR(
    devtools/ya/yalibrary/loggers/display_log
    devtools/ya/yalibrary/loggers/file_log
    devtools/ya/yalibrary/loggers/in_memory_log
)

END()

RECURSE(
    file_log
)

RECURSE_FOR_TESTS(
    tests
)
