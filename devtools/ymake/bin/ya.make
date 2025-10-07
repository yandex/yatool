PROGRAM(ymake)

IF (OS_LINUX)
    ALLOCATOR(TCMALLOC_TC)
ELSEIF (NOT OS_DARWIN)
    ALLOCATOR(GOOGLE)
ENDIF()

USE_PYTHON3()

PEERDIR(
    library/cpp/getopt
    devtools/ymake
    ${STUB_PEERDIRS}  # For ymake/stub
)

SRCS(
    main.cpp
)

# https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation#enable-long-paths-in-windows-10-version-1607-and-later
WINDOWS_LONG_PATH_MANIFEST()

END()
