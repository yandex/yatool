PY3_LIBRARY()

PY_SRCS(
    results_accumulator.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/test_types
    devtools/ya/test/util
    library/python/cores
)

END()

RECURSE_FOR_TESTS(
    benchmark
    tests
)
