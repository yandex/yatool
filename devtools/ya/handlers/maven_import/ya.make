PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/yarg
    devtools/ya/jbuild
    devtools/ya/yalibrary/tools
)

END()

RECURSE_FOR_TESTS(
    bin
)
