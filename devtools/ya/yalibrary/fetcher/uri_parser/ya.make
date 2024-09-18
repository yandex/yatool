PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.fetcher.uri_parser
    __init__.py
)

PEERDIR(
    devtools/ya/test/const
)

END()

RECURSE_FOR_TESTS(
    tests
)
