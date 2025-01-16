PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/build
    devtools/ya/build/build_opts
    devtools/ya/build/sem_graph
    devtools/ya/core/config
    devtools/ya/core/stage_tracer
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/platform_matcher
)

END()
