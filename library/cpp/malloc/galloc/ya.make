LIBRARY()

NO_UTIL()

ALLOCATOR_IMPL()

NO_BUILD_IF(OS_DARWIN)

PEERDIR(
    library/cpp/malloc/api
    contrib/deprecated/galloc
)

SRCS(
    malloc-info.cpp
)

END()
