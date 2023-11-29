PY23_LIBRARY()

PY_SRCS(ac.pyx)

PEERDIR(
    devtools/local_cache/ac/proto
    devtools/local_cache/psingleton/python
)

END()

RECURSE_FOR_TESTS(ut)
