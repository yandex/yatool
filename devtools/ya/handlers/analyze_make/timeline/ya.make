PY3_LIBRARY()

PY_SRCS(
    NAMESPACE handlers.analyze_make.timeline
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/tools/analyze_make/common
)

STYLE_PYTHON()

END()
