PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.guards
    __init__.py
)

END()

RECURSE(
    tests
)
