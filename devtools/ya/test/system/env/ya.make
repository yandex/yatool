PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE test.system.env
    __init__.py
)

PEERDIR(
    devtools/ya/core/sec
    devtools/ya/test/const
    library/python/func
)

END()
