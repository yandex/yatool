PY23_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/future
)

END()

RECURSE(
    tests
)
