PY23_LIBRARY()

PY_SRCS(
    NAMESPACE yalibrary.tools
    __init__.py
)

PEERDIR(
    devtools/libs/yaplatform/python
    devtools/ya/conf
    devtools/ya/core/config
    devtools/ya/exts
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/platform_matcher
)

IF (PYTHON2)
    PEERDIR(
        contrib/deprecated/python/typing
    )
ENDIF()

END()
