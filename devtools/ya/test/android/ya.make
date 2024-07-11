PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE test.android
    android_emulator.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/system/process
    library/python/testing/yatest_common
)

END()
