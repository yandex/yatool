PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    build_plan.pyx
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/yalibrary/platform_matcher
)

END()
