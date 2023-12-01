PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    run_go_test.py
)

PEERDIR(
    devtools/ya/exts
    library/python/testing/yatest_lib
)

END()

RECURSE_FOR_TESTS(
    tests
)
