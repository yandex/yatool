PY3_LIBRARY()

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
        devtools/ya/yalibrary/fetcher/ufetcher
    )
ENDIF()

END()

RECURSE(
    tests/sandbox_storage
)
