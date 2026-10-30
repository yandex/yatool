PY23_LIBRARY()

PY_SRCS(
    __init__.py
    compact.pyx
    dumper.pyx
    loader.pyx
)

SRCS(
    lib/compact.cpp
    lib/encoder.cpp
    lib/dump.cpp
    lib/load.cpp
    lib/filter.cpp
    lib/native_input.cpp
    lib/rewrite.cpp
)

PEERDIR(
    contrib/python/six
    devtools/libs/json_sax
    library/cpp/pybind
    library/cpp/ucompress
)

END()

RECURSE_FOR_TESTS(
    fat
    tests
)
