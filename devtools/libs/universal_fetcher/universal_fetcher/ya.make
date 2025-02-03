LIBRARY()

SRCS(
    fetchers_interface.cpp
    universal_fetcher.cpp
)

GENERATE_ENUM_SERIALIZATION(fetchers_interface.h)

PEERDIR(
    library/cpp/logger
    devtools/libs/universal_fetcher/utils/checksum
)

END()
