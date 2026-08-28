PY3_LIBRARY()

PY_SRCS(
    __init__.py
    classify.py
    log_handler.py
    projection.py
    subscriber.py
)

PEERDIR(
    devtools/ya/core/error
    devtools/ya/core/event_handling
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/loggers/file_log
    library/python/strings
)

END()

RECURSE_FOR_TESTS(
    tests
)
