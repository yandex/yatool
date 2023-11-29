PY23_LIBRARY()

PY_SRCS(
    run_g_benchmark.py
)

PEERDIR(
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/programs/test_tool/lib/benchmark
    library/python/testing/yatest_lib
)

END()

RECURSE_FOR_TESTS(
    tests
)
