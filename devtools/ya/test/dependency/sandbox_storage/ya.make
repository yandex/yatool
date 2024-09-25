PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app_config
    devtools/ya/exts
    devtools/ya/core/config
)

IF (NOT OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/yandex/sandbox
    )
ENDIF()

END()

RECURSE(
    tests/sandbox_storage
)
