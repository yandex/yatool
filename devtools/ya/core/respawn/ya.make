PY23_LIBRARY()

PEERDIR(
    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/yalibrary/find_root
)

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.respawn
    __init__.py
)

END()
