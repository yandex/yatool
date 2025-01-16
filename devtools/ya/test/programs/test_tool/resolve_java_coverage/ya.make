PY3_LIBRARY()

PY_SRCS(
    resolve_java_coverage.py
)

PEERDIR(
    contrib/deprecated/python/ujson
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/programs/test_tool/lib/runtime
    devtools/ya/test/util
    library/python/testing/coverage_utils
)

END()

RECURSE_FOR_TESTS(
    tests
)
