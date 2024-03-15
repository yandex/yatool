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
    devtools/executor/proc_util
    library/cpp/logger/global
)

END()
