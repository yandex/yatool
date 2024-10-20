PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE handlers.dump.debug
    __init__.py
)

PEERDIR(
    contrib/python/pathlib2
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core/common_opts
    devtools/ya/core/logger
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/debug_store/processor
    devtools/ya/yalibrary/yandex/sandbox/misc
    devtools/ya/yalibrary/evlog
)

IF (NOT YA_OPENSOURCE)
    PY_SRCS(
        NAMESPACE handlers.dump.debug
        dump_upload.py
    )
    PEERDIR(
        devtools/ya/yalibrary/upload
    )
ENDIF()

END()
