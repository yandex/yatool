PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app/modules/evlog
    devtools/ya/app/modules/params
    devtools/ya/app/modules/token_suppressions
    devtools/ya/app_config
    devtools/ya/build/targets
    devtools/ya/core/config
    devtools/ya/core/error
    devtools/ya/core/event_handling
    devtools/ya/core/gsid
    devtools/ya/core/logger
    devtools/ya/core/monitoring
    devtools/ya/core/report
    devtools/ya/core/report/parse_events_filter
    devtools/ya/core/respawn
    devtools/ya/core/stage_aggregator
    devtools/ya/core/stage_tracer
    devtools/ya/core/sec
    devtools/ya/core/sig_handler
    devtools/ya/core/user
    devtools/ya/exts
    devtools/ya/yalibrary/active_state
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/build_graph_cache/changelist_storage
    devtools/ya/yalibrary/debug_store
    devtools/ya/yalibrary/debug_store/store
    devtools/ya/yalibrary/display
    devtools/ya/yalibrary/evlog
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/host_health
    devtools/ya/yalibrary/loggers
    devtools/ya/yalibrary/loggers/display_log
    devtools/ya/yalibrary/loggers/file_log
    devtools/ya/yalibrary/profiler
    devtools/ya/yalibrary/showstack
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/vcs/vcsversion
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/oauth
        devtools/ya/yalibrary/yandex/sandbox
        devtools/ya/yalibrary/diagnostics
    )
ENDIF()

END()
