PY23_LIBRARY()

PY_SRCS(ac.pyx)

IF (PYTHON2)
    SRCS(ac2.cpp)
ELSE()
    SRCS(ac.cpp)
ENDIF()

PEERDIR(
    devtools/local_cache/ac/proto
    devtools/local_cache/psingleton/python
    library/cpp/pybind
)

END()

RECURSE_FOR_TESTS(ut)
