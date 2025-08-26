PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/humanfriendly
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/store/yt_store
    devtools/ya/yalibrary/store/yt_store/opts_helper
)

END()

RECURSE(
    bin
)

RECURSE_FOR_TESTS(
    tests
)
