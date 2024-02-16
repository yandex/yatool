PY23_LIBRARY()

STYLE_PYTHON()

PEERDIR(
    contrib/python/zstandard
)

PY_SRCS(
    NAMESPACE yalibrary.evlog
    __init__.py
)

END()
