PY3_LIBRARY()

PY_SRCS(
    run_pyimports.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/test/const
    library/python/cores
)

END()

RECURSE_FOR_TESTS(
    tests
)
