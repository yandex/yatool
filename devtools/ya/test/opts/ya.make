PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

STYLE_PYTHON()

PEERDIR(
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/upload/consts
    library/python/func
)

END()

RECURSE_FOR_TESTS(
    tests
)
