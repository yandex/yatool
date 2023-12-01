PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.build_plan
    __init__.py
    build_plan.pyx
)

PEERDIR(
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/yalibrary/platform_matcher
)

END()
