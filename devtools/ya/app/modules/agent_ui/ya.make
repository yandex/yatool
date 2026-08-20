PY3_LIBRARY()

PY_SRCS(
    __init__.py
)

PEERDIR(
    devtools/ya/app/modules/caller_info
    devtools/ya/core/common_opts
    devtools/ya/core/report
    devtools/ya/yalibrary/agent_ui
)

END()

RECURSE_FOR_TESTS(
    tests
)
