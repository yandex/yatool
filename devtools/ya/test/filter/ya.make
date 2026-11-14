PY3_LIBRARY()

PY_SRCS(
    # TODO refactor this module
    __init__.py
)

PEERDIR(
    devtools/ya/test/suite_storage
    contrib/python/six
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/test_types
)

END()

RECURSE_FOR_TESTS(
    tests
)
