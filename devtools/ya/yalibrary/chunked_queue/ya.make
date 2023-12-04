PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.chunked_queue
    __init__.py
    queue.py
)

PEERDIR(
    devtools/ya/exts
)

END()

RECURSE(
    tests
)
