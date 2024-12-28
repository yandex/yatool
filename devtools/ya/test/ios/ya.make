PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    simctl_control.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/test/system/process
)

END()
