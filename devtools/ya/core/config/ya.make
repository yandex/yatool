PY23_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/core/resource
    devtools/ya/exts
    devtools/ya/yalibrary/find_root
    contrib/python/six
)

IF (PYTHON3)
    PEERDIR(
        devtools/ya/core/user
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
