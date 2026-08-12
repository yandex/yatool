PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya_make/libs/python_larry_client
)

END()

RECURSE_FOR_TESTS(
    tests
)
