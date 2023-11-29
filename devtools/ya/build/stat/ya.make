PY23_LIBRARY()

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
    devtools/ya/core
)

STYLE_PYTHON()

END()

RECURSE(
    tests
)
