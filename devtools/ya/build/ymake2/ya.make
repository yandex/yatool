PY3_LIBRARY()

PY_SRCS(
    __init__.py
    consts.py
    run_ymake.pyx
)

SRCS(
    run_ymake.cpp
)

PEERDIR(
    devtools/ya/core/config
    devtools/ya/core/error
    devtools/ya/core/event_handling
    devtools/ya/core/report
    devtools/ya/core/yarg
    devtools/ya/exts
    devtools/ya/yalibrary/guards
    devtools/ya/yalibrary/tools
    devtools/ya/build/ccgraph
    devtools/ya/build/genconf
    devtools/ya/build/prefetch
    devtools/ya/yalibrary/find_root
    library/cpp/blockcodecs
    library/cpp/pybind
    library/cpp/ucompress
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        devtools/ya/yalibrary/diagnostics
        devtools/ya/yalibrary/build_graph_cache
    )
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
