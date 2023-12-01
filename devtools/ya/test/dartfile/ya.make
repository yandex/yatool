PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/test/const
    library/python/strings
)

END()

RECURSE_FOR_TESTS(
    tests
)
