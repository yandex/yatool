PY23_LIBRARY()

PY_SRCS(
    NAMESPACE core.profiler
    __init__.py
)

PEERDIR(
    contrib/python/six

    devtools/ya/exts
)

STYLE_PYTHON()

END()
