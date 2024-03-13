PY3_LIBRARY()

PY_SRCS(
    NAMESPACE handlers.analyze_make
    __init__.py
)

PEERDIR(
    devtools/ya/exts
    devtools/ya/core/yarg
    devtools/ya/yalibrary/app_ctx
    devtools/ya/yalibrary/tools
    devtools/ya/app/modules/evlog
    devtools/ya/app/modules/params
    devtools/ya/app/modules/token_suppressions
    devtools/ya/app_config
    devtools/ya/handlers/analyze_make/timeline
)


END()

RECURSE_FOR_TESTS(
    bin
    tests
)
