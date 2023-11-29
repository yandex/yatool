PY23_LIBRARY()

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
    library/cpp/blockcodecs
    library/cpp/pybind
    library/cpp/ucompress
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(devtools/ya/yalibrary/diagnostics)
ENDIF()

END()

RECURSE_FOR_TESTS(
    tests
)
