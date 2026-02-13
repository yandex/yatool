PY3_LIBRARY()

PEERDIR(
    contrib/python/psutil
    devtools/ya/core
)

PY_SRCS(
    __init__.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
