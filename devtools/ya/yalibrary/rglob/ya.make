PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.rglob
    __init__.py
)

END()

RECURSE(
    tests
)
