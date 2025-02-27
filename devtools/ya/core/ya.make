PY23_LIBRARY()

PEERDIR(
    contrib/python/pylev
    contrib/python/toml
    devtools/ya/conf
    devtools/ya/core/config
    devtools/ya/core/error
    devtools/ya/core/event_handling
    devtools/ya/core/gsid
    devtools/ya/core/imprint
    devtools/ya/core/logger
    devtools/ya/core/patch_tools
    devtools/ya/core/plugin_loader
    devtools/ya/core/profiler
    devtools/ya/core/report
    devtools/ya/core/resource
    devtools/ya/core/respawn
    devtools/ya/core/stage_aggregator
    devtools/ya/core/stage_tracer
    devtools/ya/core/stages_profiler
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/upload/consts
)

IF (PYTHON3)
    PEERDIR(
        devtools/ya/core/monitoring
        devtools/ya/core/user
        devtools/ya/core/yarg
        devtools/ya/core/common_opts
    )
ENDIF()

END()
