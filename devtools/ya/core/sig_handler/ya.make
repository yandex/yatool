PY23_LIBRARY()

PY_SRCS(
    NAMESPACE core.sig_handler
    __init__.py
)

PEERDIR(
    contrib/python/termcolor
)

STYLE_PYTHON()

END()
