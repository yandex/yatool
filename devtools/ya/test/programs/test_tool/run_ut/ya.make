PY3_LIBRARY()

PY_SRCS(
    run_ut.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/test_types
    devtools/ya/test/util
    library/python/coredump_filter
    library/python/cores
    library/python/testing/yatest_common
    library/python/testing/yatest_lib
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    benchmark
    tests
)
