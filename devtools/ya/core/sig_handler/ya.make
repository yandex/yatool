PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    contrib/python/termcolor
    devtools/executor/proc_util/python
)

STYLE_PYTHON()

END()
