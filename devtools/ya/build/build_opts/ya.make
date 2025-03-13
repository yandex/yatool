PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/humanfriendly
    devtools/ya/core/common_opts
    devtools/ya/core/error
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/test/opts
    devtools/ya/yalibrary/runner/fs
    devtools/ya/yalibrary/runner/schedule_strategy
    devtools/ya/yalibrary/store/yt_store/opts_helper
    devtools/ya/yalibrary/upload/consts
    devtools/ya/yalibrary/vcs
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/build/build_opts/build_graph_cache
        devtools/ya/build/build_opts/distbs
        devtools/ya/build/build_opts/checkout
    )
ENDIF()

END()
