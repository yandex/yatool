PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(__init__.py)

PEERDIR(
    devtools/ya/exts
    library/python/windows
)

END()

RECURSE_FOR_TESTS(
    tests
)
