PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    containers.py
    meta_info.py
    testcase.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/jbuild/gen/consts
    devtools/ya/test/const
    library/python/strings
)

END()
