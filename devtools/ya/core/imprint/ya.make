PY23_LIBRARY()

PY_SRCS(
    __init__.py
    base.py
    change_list.py
    imprint.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/test/error
    # devtools/ya/yalibrary/monitoring
)

END()
