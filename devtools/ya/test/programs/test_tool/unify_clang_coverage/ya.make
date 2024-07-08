PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    unify_clang_coverage.py
)

PEERDIR(
    devtools/ya/test/programs/test_tool/lib/coverage
    library/python/testing/coverage_utils
)

END()

IF (OS_LINUX)
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
