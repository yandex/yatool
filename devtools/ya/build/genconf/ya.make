PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/core/config
    devtools/ya/core/report
    devtools/ya/exts
    devtools/ya/yalibrary/guards
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/tools
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/diagnostics
    )
ENDIF()

END()
