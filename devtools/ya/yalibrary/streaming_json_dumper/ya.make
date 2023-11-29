PY23_LIBRARY()

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
