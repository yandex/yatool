PY23_LIBRARY()

PY_SRCS(
    __init__.py
    containers.py
    testcase.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/test/const
    library/python/strings
)

END()
