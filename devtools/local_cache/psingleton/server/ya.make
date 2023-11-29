LIBRARY(psingleton)

SRCS(
    config.cpp
    server.cpp
)

IF (NOT OS_WINDOWS)
    SRCS(
        server-nix.cpp
    )
ENDIF()

PEERDIR(
    devtools/local_cache/psingleton/service
    library/cpp/sqlite3
    library/cpp/deprecated/atomic
)

GENERATE_ENUM_SERIALIZATION(config.h)

END()
