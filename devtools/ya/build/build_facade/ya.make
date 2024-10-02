PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.build_facade
    __init__.py
)

PEERDIR(
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/build/evlog
    devtools/ya/build/genconf
    devtools/ya/build/gen_plan
    devtools/ya/build/ymake2
)

END()
