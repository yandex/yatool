PY23_LIBRARY()

PEERDIR(
    contrib/python/distro
    devtools/ya/core/sec
    devtools/ya/core/config
    devtools/ya/core/gsid
    devtools/ya/exts
    devtools/ya/yalibrary/chunked_queue
    # devtools/ya/yalibrary/snowden
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/snowden
    )
ENDIF()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core.report
    __init__.py
)

END()
