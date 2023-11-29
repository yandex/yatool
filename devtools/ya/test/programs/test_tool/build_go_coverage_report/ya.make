PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(build_go_coverage_report.py)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/util
)

END()

RECURSE_FOR_TESTS(
    tests
)
