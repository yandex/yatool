PY3_LIBRARY()

PY_SRCS(__init__.py)

PEERDIR(
    devtools/ya/build/graph_description
    devtools/ya/exts
    devtools/ya/test/test_node/cmdline
)

END()
