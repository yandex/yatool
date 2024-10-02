PY23_LIBRARY()

PY_SRCS(
    NAMESPACE core.stage_tracer
    __init__.py
)

PEERDIR(
    devtools/ya/core/profiler
    devtools/ya/core/stage_aggregator
    devtools/ya/core/stages_profiler
)

STYLE_PYTHON()

END()
