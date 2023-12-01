PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    ytexec_run_test.py
)

PEERDIR(
    contrib/python/psutil
    devtools/ya/exts
    devtools/ya/test/programs/test_tool/run_test
    library/python/testing/yatest_common
    yt/python/yt/yson
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/oauth
    )
ENDIF()

END()

IF (NOT OS_WINDOWS)
    RECURSE_FOR_TESTS(
        tests
    )
ENDIF()
