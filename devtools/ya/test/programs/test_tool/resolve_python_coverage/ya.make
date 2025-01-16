PY3_LIBRARY()

PY_SRCS(
    resolve_python_coverage.py
)

PEERDIR(
    contrib/python/coverage
    contrib/deprecated/python/ujson
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/runtime
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/util
)

END()

IF (NOT OS_WINDOWS)  # YA-1973
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
