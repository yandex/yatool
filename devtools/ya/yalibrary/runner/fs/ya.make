PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.runner.fs
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/build/build_opts
)

END()

RECURSE()
