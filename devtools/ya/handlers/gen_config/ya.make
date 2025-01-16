PY3_LIBRARY()

PY_SRCS(
    __init__.py
    gen_config.py
)

PEERDIR(
    contrib/python/six
    contrib/python/toml
    devtools/ya/core/common_opts
    devtools/ya/core/yarg
)

END()

RECURSE_FOR_TESTS(
    bin
    tests
)
