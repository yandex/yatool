PY23_LIBRARY()

PEERDIR(
    devtools/libs/acdigest/python
    devtools/ya/app_config
    devtools/ya/core/config
    devtools/ya/core/report
    devtools/ya/exts
    devtools/ya/yalibrary/evlog
    contrib/python/grpcio
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/distbuild/libs/gsid_classifier/python
    )
ENDIF()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.toolscache
    __init__.py
)

IF (NOT OS_WINDOWS)
    PEERDIR(
        devtools/local_cache/toolscache/python
        devtools/local_cache/ac/python
    )
ENDIF()

END()
