PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.platform_matcher
    __init__.py
    matcher.py
    platform_params.py
)

PEERDIR(
    contrib/python/PyYAML
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
