PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    base_options.py
    ya_options.py
    ya_runner.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/exts
    devtools/autocheck/ya_helper/common
    devtools/ya/yalibrary/platform_matcher
)

END()

RECURSE_FOR_TESTS(
    tests
)
