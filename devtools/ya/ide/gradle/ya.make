PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE ide.gradle
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
