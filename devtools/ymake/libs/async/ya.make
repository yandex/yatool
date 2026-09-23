LIBRARY()

SRCS(parallel.cpp)
PEERDIR(contrib/libs/asio)

END()

RECURSE_FOR_TESTS(ut)
