PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/build
    devtools/ya/build/build_opts
)

END()

RECURSE_FOR_TESTS(
    bin
    tests
)
