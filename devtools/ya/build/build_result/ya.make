PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.build_result
    __init__.py
)

PEERDIR(
    devtools/ya/build/build_plan
    devtools/ya/build/graph_description
    devtools/ya/build/node_checks
)

END()
