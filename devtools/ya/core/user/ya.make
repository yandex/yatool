PY3_LIBRARY()

PEERDIR(
    library/python/user_class
)

PY_SRCS(
    __init__.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
