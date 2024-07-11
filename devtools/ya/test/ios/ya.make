PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE test.ios
    simctl_control.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/system/process
)

END()
