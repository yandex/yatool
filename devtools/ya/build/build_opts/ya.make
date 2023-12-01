PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.build_opts
    __init__.py
)

PEERDIR(
    contrib/python/humanfriendly
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/test/opts
    devtools/ya/yalibrary/runner
    devtools/ya/yalibrary/runner/fs
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

IF (NEBIUS)
    PEERDIR(
        devtools/ya/build/build_opts/build_graph_cache
        devtools/ya/build/build_opts/distbs
    )
ENDIF()

END()
