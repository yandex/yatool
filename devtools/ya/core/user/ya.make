PY3_LIBRARY()

PEERDIR(
    devtools/ya/core/user/consts
)

PY_SRCS(
    __init__.py
)

END()

RECURSE(
    consts
)

RECURSE_FOR_TESTS(
    tests
)
