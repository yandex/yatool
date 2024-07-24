LIBRARY()
SRCS(greet.cpp)
PEERDIR(contrib/libs/fmt)

END()

RECURSE(bin)
RECURSE_FOR_TESTS(ut)
