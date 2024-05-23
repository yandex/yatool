PY23_LIBRARY()

PY_SRCS(
    NAMESPACE build.prefetch
    __init__.py
)

PEERDIR(
    devtools/ya/yalibrary/vcs
    devtools/ya/yalibrary/tools
    devtools/ya/exts
    devtools/ya/core/event_handling
)

END()

RECURSE_FOR_TESTS(
    tests
)
