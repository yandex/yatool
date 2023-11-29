PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.error
    __init__.py
)

PEERDIR(
    contrib/python/future
)

END()

RECURSE(
    tests
)
