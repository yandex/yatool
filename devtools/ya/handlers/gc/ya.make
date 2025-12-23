PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/app
    devtools/ya/build
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/runner
    devtools/ya/yalibrary/store
    devtools/ya/yalibrary/toolscache
    library/python/fs
    library/python/windows
)

END()

RECURSE_FOR_TESTS(
    bin
    tests
)
