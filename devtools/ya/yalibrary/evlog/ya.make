PY23_LIBRARY()

PEERDIR(
    contrib/python/zstandard
    devtools/ya/core/config
    devtools/ya/core/gsid
)

PY_SRCS(
    NAMESPACE yalibrary.evlog
    __init__.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
