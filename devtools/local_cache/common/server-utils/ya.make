LIBRARY()

SRCS(
    server.cpp
)

IF (OS_LINUX OR OS_DARWIN OR OS_IOS)
    SRCS(
        server-nix.cpp
    )
ENDIF()

PEERDIR(
    devtools/executor/lib
    library/cpp/logger/global
)

END()
