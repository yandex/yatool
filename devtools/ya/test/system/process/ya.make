PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE test.system.process
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    library/python/testing/yatest_common
)

END()
