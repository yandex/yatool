PY3_LIBRARY()

PY_SRCS(
    __init__.py
    helpers.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/core/yarg
    devtools/ya/build
    devtools/ya/build/build_opts
    devtools/ya/test/opts
)

END()

RECURSE_FOR_TESTS(
    bin
)
