PY3_LIBRARY()

PEERDIR(
    devtools/ya/exts
    devtools/ya/yalibrary/store
    devtools/ya/yalibrary/runner
)

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.last_failed
    last_failed.py
)

END()
