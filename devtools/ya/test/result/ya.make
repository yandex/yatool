PY23_LIBRARY()

PY_SRCS(
    NAMESPACE test.result
    # TODO refactor module
    __init__.py
)

PEERDIR(
    contrib/python/six
    devtools/ya/app_config
    devtools/ya/build/build_result
    devtools/ya/core
    devtools/ya/exts
    devtools/ya/test/common
    devtools/ya/test/const
    devtools/ya/yalibrary/platform_matcher
    library/python/strings
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/distbuild/libs/node_status/python
    )
ENDIF()

STYLE_PYTHON()

END()

RECURSE_FOR_TESTS(
    tests
)
