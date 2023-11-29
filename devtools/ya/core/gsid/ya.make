PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.gsid
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/core/config
)

END()
