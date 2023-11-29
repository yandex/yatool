PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.app_ctx
    __init__.py
)

PEERDIR(
    contrib/python/contextlib2
)

END()
