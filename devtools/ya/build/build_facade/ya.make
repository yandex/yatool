PY23_LIBRARY()

PY_SRCS(
    NAMESPACE build.build_facade

    __init__.py
)

PEERDIR(
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/build/evlog
    devtools/ya/build/genconf
    devtools/ya/build/gen_plan
    devtools/ya/build/ymake2
)

END()
