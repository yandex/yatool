PY23_LIBRARY()

PEERDIR(
    devtools/libs/parse_number
    contrib/python/six
)

PY_SRCS(
    parse_number.pyx
)

END()
