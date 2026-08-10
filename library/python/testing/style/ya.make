PY3_LIBRARY()

PY_SRCS(
    rules.py
)

PEERDIR(
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    tests
)
