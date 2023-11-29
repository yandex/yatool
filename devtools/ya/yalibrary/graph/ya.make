PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.graph
    base.py
    const.py
    commands.py
    node.py
)

PEERDIR(
    devtools/ya/core
    devtools/ya/exts
)

END()

RECURSE_FOR_TESTS(
    tests
)
