PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.monitoring
    __init__.py
)

PEERDIR(
    devtools/ya/yalibrary/app_ctx
)

END()
