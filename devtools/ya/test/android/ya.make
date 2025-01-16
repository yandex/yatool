PY3_LIBRARY()

PY_SRCS(
    android_emulator.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/system/process
    library/python/testing/yatest_common
)

END()
