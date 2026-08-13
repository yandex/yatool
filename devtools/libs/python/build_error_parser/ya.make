PY3_LIBRARY()

PY_SRCS(
    __init__.py
    build_error_parser.py
    configure_cache.py
    error_base.py
    error_collections.py
    errors.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/core/error
)

END()

RECURSE_FOR_TESTS(
    tests
)
