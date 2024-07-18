PY23_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/exts
)

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    tests
)
