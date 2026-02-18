PY3_LIBRARY()

PY_SRCS(
    __init__.py
    dump_integration.py
    layout.py
    target_tc.py
)

PEERDIR(
    devtools/ya/exts
)

END()

