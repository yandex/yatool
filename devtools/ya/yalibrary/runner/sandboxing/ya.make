PY3_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE yalibrary.runner.sandboxing
    __init__.py
    opensource_sandboxing.py
)

IF (NOT YA_OPENSOURCE)
    PY_SRCS(
        NAMESPACE yalibrary.runner.sandboxing
        yandex_sandboxing.py
    )
    PEERDIR(
        devtools/ya/exts
        devtools/ya/yalibrary/platform_matcher
    )
    IF (NOT OS_WINDOWS)
        PEERDIR(
            devtools/ya/yalibrary/runner/sandboxing/fusefs/python
        )
    ENDIF()
ENDIF()

END()

IF (NOT YA_OPENSOURCE)
    RECURSE(
        fusefs
        tests
    )
ENDIF()
