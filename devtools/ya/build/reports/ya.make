PY23_LIBRARY()

PY_SRCS(
    NAMESPACE build.reports
    build_reports.py
    autocheck_report.py
    results_listener.py
    configure_error.py
)

PEERDIR(
    devtools/ya/build/owners
    devtools/ya/build/stat
    devtools/ya/exts
    devtools/ya/test/reports
    devtools/ya/test/result
    devtools/ya/test/test_node
    devtools/ya/test/util
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/term
    library/python/testing/yatest_lib
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/yandex/distbuild
    )
ENDIF()

END()
