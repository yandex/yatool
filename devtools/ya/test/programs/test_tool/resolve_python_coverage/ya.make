PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(resolve_python_coverage.py)

PEERDIR(
    contrib/python/coverage
    contrib/deprecated/python/ujson
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/runtime
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/util
)

END()

RECURSE_FOR_TESTS(
    tests
)
