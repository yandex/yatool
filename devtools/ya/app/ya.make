PY23_LIBRARY()

PY_SRCS(
    NAMESPACE app
    __init__.py
)

PEERDIR(
    devtools/ya/app/modules/evlog
    devtools/ya/app/modules/params
    devtools/ya/app/modules/token_suppressions
    devtools/ya/app_config
    devtools/ya/build/targets
    devtools/ya/core
    devtools/ya/core/sec
    devtools/ya/core/sig_handler
    devtools/ya/exts
    devtools/ya/yalibrary/active_state
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/debug_store
    devtools/ya/yalibrary/debug_store/store
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/evlog
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/loggers
    devtools/ya/yalibrary/loggers/display_log
    devtools/ya/yalibrary/loggers/file_log
    devtools/ya/yalibrary/profiler
    devtools/ya/yalibrary/showstack
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/ya_helper/ya_utils
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/oauth
        devtools/ya/yalibrary/yandex/sandbox
        devtools/ya/yalibrary/diagnostics
    )
ENDIF()

STYLE_PYTHON()

END()
