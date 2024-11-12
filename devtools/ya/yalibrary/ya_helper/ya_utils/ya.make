PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.ya_helper.ya_utils
    __init__.py
    base_options.py
    ya_options.py
    ya_runner.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/exts
    devtools/ya/yalibrary/ya_helper/common
    devtools/ya/yalibrary/platform_matcher
)

END()

RECURSE_FOR_TESTS(
    tests
)
