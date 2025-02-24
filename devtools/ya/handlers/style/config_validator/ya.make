PY3_LIBRARY()

PEERDIR(
    contrib/python/PyYAML
)

PY_SRCS(
    __init__.py
    config.py
    rules.py
    validator.py
)

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    tests
)
