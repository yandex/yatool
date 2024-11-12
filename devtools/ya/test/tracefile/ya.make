PY23_LIBRARY()

PY_SRCS(
    __init__.py
    CYTHONIZE_PY
    tracefile.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/facility
    devtools/ya/yalibrary/formatter
    library/python/strings
)

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    tests
)
