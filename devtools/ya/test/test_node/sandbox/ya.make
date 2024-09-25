PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/build/gen_plan
    devtools/ya/test/dependency
    devtools/ya/test/dependency/sandbox_storage
    devtools/ya/test/system
    devtools/ya/test/util
)

END()
