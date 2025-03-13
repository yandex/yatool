PY3_LIBRARY()

PY_SRCS(__init__.py)

PEERDIR(
    contrib/python/humanfriendly
)

END()

RECURSE_FOR_TESTS(tests)
