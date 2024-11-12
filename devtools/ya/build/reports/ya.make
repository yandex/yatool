PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.reports
    autocheck_report.py
    build_reports.py
    configure_error.py
    results_listener.py
    results_report.py
)

PEERDIR(
    contrib/python/multidict
    devtools/ya/build/owners
    devtools/ya/build/stat
    devtools/ya/exts
    devtools/ya/test/reports
    devtools/ya/test/result
    devtools/ya/test/test_node
    devtools/ya/test/util
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/sjson
    devtools/ya/yalibrary/term
    library/python/testing/yatest_lib
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/yandex/distbuild
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
