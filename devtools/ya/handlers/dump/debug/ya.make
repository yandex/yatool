PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/logger
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/handlers/dump/reproducer
    devtools/ya/yalibrary/debug_store/processor
    devtools/ya/yalibrary/yandex/sandbox/misc
    devtools/ya/yalibrary/evlog
)

IF (NOT YA_OPENSOURCE)
    PY_SRCS(
        dump_upload.py
    )
    PEERDIR(
        devtools/ya/yalibrary/upload
    )
ENDIF()

END()
