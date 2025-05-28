PY3_LIBRARY()

PEERDIR(
    contrib/libs/re2
    contrib/libs/yaml
    library/cpp/pybind
)

SRCS(
    ymakeyaml.cpp
)

PY_REGISTER(ymakeyaml)

END()

RECURSE(
    tests
)
