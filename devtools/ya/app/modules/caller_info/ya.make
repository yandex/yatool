PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/psutil
)

END()

RECURSE_FOR_TESTS(
    tests
)
