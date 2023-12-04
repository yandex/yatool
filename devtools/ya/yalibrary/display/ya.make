PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.display
    __init__.py
)

PEERDIR(
    devtools/ya/yalibrary/formatter
    contrib/python/colorama
)

END()

RECURSE(
    tests
)
