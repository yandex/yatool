PY3_LIBRARY()

PY_SRCS(
    NAMESPACE handlers.krevedko
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/core
    devtools/ya/core/yarg
)

NO_LINT()

END()

RECURSE_FOR_TESTS(
    bin
)
