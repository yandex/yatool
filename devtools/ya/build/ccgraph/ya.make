PY23_LIBRARY()

SRCS(
    ccgraph.cpp
)

STYLE_PYTHON()

PY_SRCS(
    __init__.py
    cpp_string_wrapper.pyx
    ccgraph.pyx
)

PEERDIR(
    devtools/ya/cpp/graph
    devtools/ya/cpp/lib/edl/python
    library/cpp/blockcodecs
    library/cpp/ucompress
)

END()
