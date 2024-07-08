PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    list_tests.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/explore
    devtools/ya/test/facility
    devtools/ya/test/filter
    devtools/ya/test/programs/test_tool/lib/testroot
    devtools/ya/test/test_types
    devtools/ya/test/util
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/test/dependency/sandbox_storage
    )
ENDIF()

END()
