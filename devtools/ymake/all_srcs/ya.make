LIBRARY()

PEERDIR(
    devtools/ymake/diag
    contrib/libs/asio
)

SRCS(
    all_srcs_context.cpp
)

IF (MSVC)
    CFLAGS(
        GLOBAL -DASIO_WINDOWS_APP
    )
ENDIF()

END()
