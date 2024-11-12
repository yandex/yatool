PY3_LIBRARY()

PY_SRCS(
    NAMESPACE core.sig_handler
    __init__.py
)

PEERDIR(
    contrib/python/termcolor
    devtools/executor/proc_util/python
)

STYLE_PYTHON()

END()
