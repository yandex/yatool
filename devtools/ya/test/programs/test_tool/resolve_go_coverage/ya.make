PY23_LIBRARY()

PY_SRCS(
    resolve_go_coverage.py
)

PEERDIR(
    contrib/python/ujson
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/programs/test_tool/lib/runtime
    devtools/ya/test/util
)

END()

RECURSE_FOR_TESTS(
    tests
)
