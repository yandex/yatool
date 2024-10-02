PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE handlers.run
    __init__.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/build
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/event_handling
    devtools/ya/core/yarg
)

END()
