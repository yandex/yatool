PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    NAMESPACE build.ymake2
    __init__.py
    consts.py
    run_ymake.pyx
)

SRCS(
    run_ymake.cpp
)

PEERDIR(
    devtools/ya/core
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
