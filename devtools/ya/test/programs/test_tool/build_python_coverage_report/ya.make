PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    build_python_coverage_report.py
)

PEERDIR(
    contrib/python/coverage
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/util
)

END()

RECURSE(
    tests
)
