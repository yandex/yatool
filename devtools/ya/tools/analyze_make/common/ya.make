PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/zstandard

    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/evlog
    devtools/ya/yalibrary/formatter
)

STYLE_PYTHON()

END()
