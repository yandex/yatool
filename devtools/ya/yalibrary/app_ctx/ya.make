PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.app_ctx
    __init__.py
)

PEERDIR(
    contrib/python/contextlib2
)

END()
