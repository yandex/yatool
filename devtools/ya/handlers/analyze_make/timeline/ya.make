PY3_LIBRARY()

PY_SRCS(
    NAMESPACE handlers.analyze_make.timeline
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/tools/analyze_make/common
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/formatter
)

STYLE_PYTHON()

END()
