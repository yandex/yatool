PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    contrib/python/six
)

END()

RECURSE_FOR_TESTS(ut)
