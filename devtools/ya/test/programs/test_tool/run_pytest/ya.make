PY3_LIBRARY()

PY_SRCS(
    run_pytest.py
)

PEERDIR(
    devtools/ya/test/const
    devtools/ya/test/system
    library/python/testing/yatest_lib
)

END()
