PY3_LIBRARY()

PY_SRCS(
    NAMESPACE handlers.buf
    __init__.py
)

RESOURCE(
    buf.yaml buf.yaml
)

PEERDIR(
    contrib/python/PyYAML
    devtools/ya/app
    devtools/ya/build/build_facade
    devtools/ya/build/build_opts
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/tools
    library/python/resource
)

END()

RECURSE_FOR_TESTS(
    bin
)
