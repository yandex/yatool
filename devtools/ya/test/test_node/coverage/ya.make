PY3_LIBRARY()

PY_SRCS(
    __init__.py
    cpp.py
    extensions.py
    go.py
    java.py
    python.py
    rigel.py
    ts.py
    upload.py
)

PEERDIR(
    devtools/ya/test/suite_storage
    devtools/ya/build/node_metadata
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/dependency
    devtools/ya/test/util
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/tools
    library/python/func
)

END()
