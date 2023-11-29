PY23_LIBRARY()

PY_SRCS(systemptr.pyx)

PEERDIR(
    devtools/local_cache/psingleton
    devtools/ya/exts
)

END()
