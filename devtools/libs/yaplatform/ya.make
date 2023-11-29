LIBRARY()

SRCS(
    platform.cpp
    platform_map.cpp
)

PEERDIR(
    library/cpp/json
)

END()

RECURSE_FOR_TESTS(
    ut
)
