PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.genconf
    __init__.py
)

PEERDIR(
    devtools/ya/core
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
