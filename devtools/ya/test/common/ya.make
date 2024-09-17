PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    ytest_common_tools.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/test/const
    library/python/testing/yatest_common
    library/python/testing/yatest_lib
)

END()
