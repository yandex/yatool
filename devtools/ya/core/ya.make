PY23_LIBRARY()

PEERDIR(
    contrib/python/pylev
    contrib/python/toml
    devtools/ya/conf
    devtools/ya/core/config
    devtools/ya/core/error
    devtools/ya/core/gsid
    devtools/ya/core/imprint
    devtools/ya/core/report
    devtools/ya/core/resource
    devtools/ya/core/respawn
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/test/const
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/fetcher
    devtools/ya/yalibrary/find_root
    devtools/ya/yalibrary/formatter
    devtools/ya/yalibrary/platform_matcher
    devtools/ya/yalibrary/tools
    devtools/ya/yalibrary/upload/consts
)

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE core
    common_opts.py
    logger.py
    patch_tools.py
    plugin_loader.py
    profiler.py
    stages_profiler.py
)

END()
