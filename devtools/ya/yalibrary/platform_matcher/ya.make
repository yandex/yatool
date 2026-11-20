PY23_LIBRARY()

IF (PYTHON3)
    # ymake test-planning workers load this module under their own GIL.
    CYTHON_FLAGS(
        -X
        subinterpreters_compatible=own_gil
    )
    CFLAGS(
        -DCYTHON_USE_MODULE_STATE=1
        -DCYTHON_USE_TYPE_SPECS=1
    )
ENDIF()

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
