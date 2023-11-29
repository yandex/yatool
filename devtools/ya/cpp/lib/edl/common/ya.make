LIBRARY()

SRCS(
    export_helpers.cpp
    loaders.cpp
    members.cpp
)

PEERDIR(
)

END()

RECURSE_FOR_TESTS(
    ut
)
