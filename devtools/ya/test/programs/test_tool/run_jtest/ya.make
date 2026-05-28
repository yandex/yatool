PY3_LIBRARY()

PY_SRCS(
    run_jtest.py
)

PEERDIR(
    devtools/ya/test/const
    devtools/ya/test/system
    library/python/testing/yatest_lib
)

END()
