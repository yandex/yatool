PY23_LIBRARY()

PY_SRCS(
    core_file.py
    run_test.py
    stages.py
    test_context.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/exts
    devtools/ya/test/canon
    devtools/ya/test/common
    devtools/ya/test/dartfile
    devtools/ya/test/dependency
    devtools/ya/test/programs/test_tool/lib/coverage
    devtools/ya/test/programs/test_tool/lib/monitor
    devtools/ya/test/programs/test_tool/lib/report
    devtools/ya/test/programs/test_tool/lib/runtime
    devtools/ya/test/programs/test_tool/lib/secret
    devtools/ya/test/programs/test_tool/lib/testroot
    devtools/ya/test/programs/test_tool/lib/tmpfs
    devtools/ya/test/programs/test_tool/lib/unshare
    devtools/ya/test/reports
    devtools/ya/test/result
    devtools/ya/test/system/env
    devtools/ya/test/system/process
    devtools/ya/test/test_types
    devtools/ya/test/tracefile
    devtools/ya/test/util
    devtools/ya/yalibrary/term
    library/python/testing/system_info
    library/python/testing/yatest_common
)

IF(NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/test/dependency/sandbox_storage
    )
ENDIF()

STYLE_PYTHON()

IF (OS_LINUX)
    PEERDIR(
        library/python/prctl
    )

    IF (NOT YA_OPENSOURCE AND ARCH_X86_64)
        PEERDIR(
            devtools/optrace/python
        )
    ENDIF()
ENDIF()

END()

RECURSE_FOR_TESTS(tests)
