LIBRARY()

SRCS(
    proc_info.cpp
)

IF (OS_LINUX)
    SRCS(
        proc_info_linux.cpp
    )
ELSEIF (OS_DARWIN OR OS_IOS)
    SRCS(
        proc_info_darwin.cpp
    )
ELSEIF (OS_WINDOWS)
    SRCS(
        proc_info_windows.cpp
    )
ENDIF()

END()
