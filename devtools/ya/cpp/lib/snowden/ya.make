LIBRARY()

SRCS(
    snowden.cpp
)

PEERDIR(
    devtools/ya/cpp/lib
    library/cpp/json
)

END()

RECURSE_FOR_TESTS(
    integration_tests
    test_helper
    ut
)
