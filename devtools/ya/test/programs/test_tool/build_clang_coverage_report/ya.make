PY3_LIBRARY()

PEERDIR(
    contrib/deprecated/python/ujson
    devtools/common/libmagic
    devtools/ya/core/resource
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/util
    library/python/testing/coverage_utils
)

PY_SRCS(
    build_clang_coverage_report.py
)

RESOURCE_FILES(
    res/style.html
)

END()

IF (OS_LINUX)
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
