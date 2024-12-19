PY3_LIBRARY()

PY_SRCS(
    NAMESPACE build.stat
    CYTHONIZE_PY
    __init__.py
    graph.py
    graph_metrics.py
    statistics.py
)

PEERDIR(
    contrib/python/humanfriendly
    devtools/ya/exts
    devtools/ya/yalibrary/status_view
    devtools/ya/yalibrary/fetcher/http_client
    devtools/ya/core/profiler
    devtools/ya/core/report
    devtools/ya/core/stage_tracer
    devtools/ya/test/const
    devtools/ya/build/node_checks
    devtools/ya/build/graph_description
)

STYLE_PYTHON()

END()

RECURSE(
    tests
)
