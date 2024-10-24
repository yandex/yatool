PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.worker_threads
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    tests
)
