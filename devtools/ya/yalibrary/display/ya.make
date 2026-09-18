PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.display
    __init__.py
    jsonl.py
)

PEERDIR(
    devtools/ya/core/error
    devtools/ya/yalibrary/formatter
    contrib/python/colorama
)

END()

RECURSE(
    tests
)
