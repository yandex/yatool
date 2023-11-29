PY23_LIBRARY()

PY_SRCS(
    run_pyimports.py
)

PEERDIR(
    library/python/cores
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    tests
)
