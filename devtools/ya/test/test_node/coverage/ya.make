PY3_LIBRARY()

STYLE_PYTHON()

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
    devtools/ya/build/gen_plan
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/test/dependency
    devtools/ya/test/dependency
    devtools/ya/test/util
    devtools/ya/yalibrary/tools
    library/python/func
)

END()
