PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.loggers.display_log
    __init__.py
)

PEERDIR(
    devtools/ya/yalibrary/status_view
)

END()

RECURSE_FOR_TESTS(
    tests
)
