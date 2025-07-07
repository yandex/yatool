PY3_LIBRARY()

PY_SRCS(
    run_hermione.py
)

PEERDIR(
    build/plugins/lib/nots/package_manager
    build/plugins/lib/nots/test_utils
    devtools/ya/test/const
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/system
    devtools/ya/test/test_types
    devtools/ya/test/util
    library/python/func
)

END()

IF (NOT OS_WINDOWS)
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
