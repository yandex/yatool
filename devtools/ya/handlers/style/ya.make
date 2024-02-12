PY23_LIBRARY()

PEERDIR(
    build/config/tests/cpp_style
    build/config/tests/py_style
    build/config/tests/ruff
    contrib/python/PyYAML
    contrib/python/coloredlogs
    contrib/python/six
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core
    devtools/ya/core/resource
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/makelists
    devtools/ya/yalibrary/tools
    library/python/color
    library/python/fs
    library/python/func
    library/python/testing/style
)

PY_SRCS(
    NAMESPACE handlers.style
    __init__.py
    style.py
    cpp_style.py
    golang_style.py
    python_style.py
    state_helper.py
    ruff_config.py
)

END()

RECURSE(
    tests
)

RECURSE_FOR_TESTS(
    bin
    tests
)
