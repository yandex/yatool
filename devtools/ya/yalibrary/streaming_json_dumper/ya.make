PY23_LIBRARY()

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    dumper.pyx
)

SRCS(
    lib/encoder.cpp
    lib/dump.cpp
)

PEERDIR(
    library/cpp/pybind
)

END()

RECURSE_FOR_TESTS(
    fat
    tests
)
