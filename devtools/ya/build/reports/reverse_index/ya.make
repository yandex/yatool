PY3_LIBRARY()

PY_SRCS(
    __init__.py
    _reverse_index.pyx
)

PEERDIR(
    devtools/ya/build/reports/reverse_index/lib
    devtools/ya/yalibrary/sjson
)

END()
