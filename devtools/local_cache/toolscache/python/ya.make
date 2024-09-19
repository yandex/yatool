PY23_LIBRARY()

PY_SRCS(toolscache.pyx)

PEERDIR(
    devtools/local_cache/toolscache/proto
    devtools/local_cache/psingleton/python
)

END()

RECURSE_FOR_TESTS(ut)
