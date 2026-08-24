LIBRARY()

SRCS(
    snowden.cpp
)

PEERDIR(
    devtools/ya/cpp/lib
)

END()

RECURSE_FOR_TESTS(
    integration_tests
    test_helper
    ut
)
