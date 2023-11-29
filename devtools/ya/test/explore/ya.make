PY23_LIBRARY()

PY_SRCS(
    NAMESPACE test.explore

    __init__.py
)

PEERDIR(
    devtools/ya/test/dartfile
    devtools/ya/test/test_types
)

END()
