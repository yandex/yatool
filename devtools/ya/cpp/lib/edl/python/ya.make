PY3_LIBRARY()

SRCS(
    from_python.cpp
    to_python.cpp
)

PEERDIR(
    devtools/ya/cpp/lib/edl/common
    library/cpp/pybind
)

END()

RECURSE_FOR_TESTS(
    tests
)
