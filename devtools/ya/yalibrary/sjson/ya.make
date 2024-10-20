PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    dumper.pyx
    loader.pyx
)

SRCS(
    lib/encoder.cpp
    lib/dump.cpp
    lib/load.cpp
)

PEERDIR(
    devtools/ya/cpp/lib/json_sax
    library/cpp/pybind
)

END()

RECURSE_FOR_TESTS(
    fat
    tests
)
