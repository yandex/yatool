PY3_LIBRARY()

PEERDIR(
    contrib/python/six
    devtools/ya/test/filter
    devtools/ya/test/programs/test_tool/lib/migrations_config
)

PY_SRCS(
    run_javastyle.py
)

END()

RECURSE_FOR_TESTS(
    tests
)
