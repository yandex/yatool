PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.config
    __init__.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/core/resource
    devtools/ya/exts
    devtools/ya/yalibrary/find_root
)

END()

RECURSE_FOR_TESTS(
    tests
)
