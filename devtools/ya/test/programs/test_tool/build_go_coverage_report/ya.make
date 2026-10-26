PY3_LIBRARY()

PY_SRCS(
    build_go_coverage_report.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/util
    library/python/testing/coverage_utils
)

END()

RECURSE_FOR_TESTS(
    tests
)
