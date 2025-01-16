PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/build/build_plan
    devtools/ya/build/graph_description
    devtools/ya/build/node_checks
)

END()
