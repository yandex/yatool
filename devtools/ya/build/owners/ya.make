PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.owners
    __init__.py
)

PEERDIR(
    devtools/ya/exts
)

END()
