PY3_LIBRARY()

PY_SRCS(
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
    devtools/ya/build/targets
)

END()
