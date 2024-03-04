PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE handlers.dump.debug
    __init__.py
)

PEERDIR(
    contrib/python/pathlib2
    devtools/ya/app
    devtools/ya/build/build_opts
    devtools/ya/core
    devtools/ya/core/config
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/debug_store/processor
    devtools/ya/yalibrary/yandex/sandbox/misc
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
