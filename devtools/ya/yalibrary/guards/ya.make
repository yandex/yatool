PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.guards
    __init__.py
)

END()

RECURSE(
    tests
)
