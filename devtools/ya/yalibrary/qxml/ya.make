PY3_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.qxml
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/exts
)

END()

RECURSE(
    tests
)
