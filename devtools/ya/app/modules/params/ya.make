PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/core/yarg
    devtools/ya/build/targets
    devtools/ya/core/respawn
    devtools/ya/exts
)

STYLE_PYTHON()

END()
