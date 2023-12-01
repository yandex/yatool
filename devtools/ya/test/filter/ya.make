PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE test.filter
    # TODO refactor this module
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/test_types
)

END()

RECURSE_FOR_TESTS(
    tests
)
