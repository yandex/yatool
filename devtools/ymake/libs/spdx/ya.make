LIBRARY()

SRCS(
    spdx.cpp
)
PEERDIR(
    devtools/ymake/libs/str_helpers
)

END()

RECURSE_FOR_TESTS(ut)
