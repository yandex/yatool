PY3_LIBRARY()

PY_SRCS(
    android_emulator.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/system/process
    library/python/port_manager
    library/python/testing/yatest_common
)

END()
