PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.runner.fs
    __init__.py
)

PEERDIR(
    devtools/ya/exts
)

END()

RECURSE()
