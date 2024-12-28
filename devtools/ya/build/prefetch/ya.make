PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/core/gsid
    devtools/ya/core/event_handling
    devtools/ya/exts
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/tools
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/distbuild/libs/gsid_classifier/python
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
