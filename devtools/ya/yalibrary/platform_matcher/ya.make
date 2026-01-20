PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.platform_matcher
    __init__.py
    matcher.pyx
    platform_params.py
    platform_params_core.py
)

PEERDIR(
    contrib/python/PyYAML
    devtools/libs/yaplatform
    devtools/ya/exts
    devtools/ya/test/const
    library/python/resource
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/typing
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
