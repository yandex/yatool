PY23_LIBRARY()

PY_SRCS(
    __init__.py
    acdigest.pyx
)

PEERDIR(
    devtools/libs/acdigest
)

END()

RECURSE_FOR_TESTS(
    tests
)
