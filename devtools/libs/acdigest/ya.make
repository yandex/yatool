LIBRARY()

SRCS(
    acdigest.cpp
)

PEERDIR(
    library/cpp/openssl/crypto
)

END()

RECURSE(
    python
)

RECURSE_FOR_TESTS(
    ut
)
