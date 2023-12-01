PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    run_fuzz.py
)

PEERDIR(
    devtools/ya/core/sec
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/test/system/env
    devtools/ya/test/test_types
    devtools/ya/test/util
    library/python/cityhash
    library/python/coredump_filter
    library/python/cores
    library/python/testing/yatest_common
)

END()

IF (OS_LINUX)
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
