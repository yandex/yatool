PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    run_boost_test.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/android
    devtools/ya/test/common
    devtools/ya/test/filter
    devtools/ya/test/ios
    devtools/ya/test/system
    devtools/ya/test/test_types
    devtools/ya/test/util
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/formatter
    library/python/testing/yatest_common
)

END()
