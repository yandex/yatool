PY3_LIBRARY()

PY_SRCS(
    canonize.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/exts
    devtools/ya/test/canon
    devtools/ya/test/const
    devtools/ya/test/dependency
    devtools/ya/test/filter
    devtools/ya/test/reports
    devtools/ya/test/result
    devtools/ya/test/test_types
    devtools/ya/test/util
    devtools/ya/yalibrary/yandex/sandbox/misc
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/test/dependency/sandbox_storage
    )
ENDIF()

END()

IF (NOT OS_WINDOWS)  # YA-1973
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
