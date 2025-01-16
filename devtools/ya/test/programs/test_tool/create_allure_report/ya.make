PY3_LIBRARY()

PY_SRCS(
    create_allure_report.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/test/reports
    devtools/ya/test/result
    devtools/ya/test/test_types
    devtools/ya/test/util
)

END()
