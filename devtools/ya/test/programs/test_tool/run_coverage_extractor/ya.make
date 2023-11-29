PY23_LIBRARY()

PY_SRCS(
    run_coverage_extractor.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/util
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/test/test_types
)

END()

IF (OS_LINUX)
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
