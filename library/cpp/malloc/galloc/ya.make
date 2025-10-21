LIBRARY()

NO_UTIL()

ALLOCATOR_IMPL()

BUILD_ONLY_IF(OS_LINUX)

PEERDIR(
    library/cpp/malloc/api
    contrib/deprecated/galloc
)

SRCS(
    malloc-info.cpp
)

END()
