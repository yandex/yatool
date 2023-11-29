PY3_LIBRARY()

PY_SRCS(
    run_y_benchmark.py
)

PEERDIR(
    devtools/ya/test/programs/test_tool/lib/benchmark
    library/python/testing/yatest_lib
)

END()

RECURSE_FOR_TESTS(tests)
