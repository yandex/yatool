PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/ide
    devtools/ya/yalibrary/platform_matcher
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/ide/fsnotifier
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    bin
    tests
)
