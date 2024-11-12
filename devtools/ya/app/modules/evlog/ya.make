PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/core/config
    devtools/ya/core/gsid
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/evlog
)

STYLE_PYTHON()

END()
