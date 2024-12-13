PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.chunked_queue
    __init__.py
    queue.py
)

PEERDIR(
    devtools/ya/exts
    contrib/python/pytz
)

IF(PYTHON2)
    PEERDIR(contrib/python/pathlib2)
    PEERDIR(contrib/deprecated/python/typing)
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
