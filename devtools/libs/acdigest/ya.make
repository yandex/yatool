LIBRARY()

SRCS(
    acdigest.cpp
)

PEERDIR(
    contrib/libs/xxhash
)

END()

RECURSE(
    python
)

RECURSE_FOR_TESTS(
    ut
)
