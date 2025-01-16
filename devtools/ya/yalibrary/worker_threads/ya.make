PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.worker_threads
    __init__.py
)

PEERDIR(
    devtools/ya/exts
)

END()

RECURSE_FOR_TESTS(
    tests
)
