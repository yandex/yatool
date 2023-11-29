PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.chunked_queue
    __init__.py
    queue.py
)

PEERDIR(
    devtools/ya/exts
)

END()

RECURSE(tests)
