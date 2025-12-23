PY23_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/core/sec
    devtools/ya/test/const
    library/python/func
)

END()

RECURSE_FOR_TESTS(
    tests
)
