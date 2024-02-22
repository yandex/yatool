PY23_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/psutil
    contrib/python/six
)

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    test
)
