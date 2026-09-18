PY23_LIBRARY()

PY_SRCS(
    __init__.py
    progress.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/yalibrary/status_view
)

END()
