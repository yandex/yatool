PY3_LIBRARY()

PY_SRCS(
    results_merger.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/reports
    devtools/ya/test/result
    devtools/ya/test/util
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(
    tests
)
