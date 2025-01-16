PY23_LIBRARY()

PY_SRCS(
    __init__.py
    build_plan.pyx
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/yalibrary/platform_matcher
)

END()
