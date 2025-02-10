PY3_LIBRARY()

PEERDIR(
    build/config/tests/cpp_style
    build/config/tests/py_style
    build/config/tests/ruff
    contrib/python/coloredlogs
    contrib/python/marisa-trie
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/resource
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/makelists
    devtools/ya/yalibrary/tools
    library/python/color
    library/python/fs
    library/python/func
    library/python/testing/style
)

PY_SRCS(
    __init__.py
    config.py
    disambiguate.py
    state_helper.py
    style.py
    styler.py
    target.py
)

END()

RECURSE(
    tests
)

RECURSE_FOR_TESTS(
    bin
    tests
)
