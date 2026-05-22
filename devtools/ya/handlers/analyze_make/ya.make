PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/core/common_opts
    devtools/ya/core/yarg
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/tools
    devtools/ya/app/modules/evlog
    devtools/ya/app/modules/params
    devtools/ya/app/modules/token_suppressions
    devtools/ya/app_config
    devtools/ya/handlers/analyze_make/graph_diff
    devtools/ya/handlers/analyze_make/timeline
    devtools/ya/handlers/analyze_make/timebloat
    devtools/ya/yalibrary/evlog
)

IF (NOT OPENSOURCE)
    PEERDIR(
        devtools/ya/handlers/analyze_make/log_viewer
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    bin
    tests
)

IF (NOT OPENSOURCE)
    RECURSE_FOR_TESTS(
        log_viewer
    )
ENDIF()
