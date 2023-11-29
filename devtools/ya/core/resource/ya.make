PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.resource
    __init__.py
)

PEERDIR(
    library/python/func
)

END()
