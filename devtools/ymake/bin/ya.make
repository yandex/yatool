PROGRAM(ymake)

IF (OS_CYGWIN)
    ALLOCATOR(SYSTEM)
# FIXME: disable for now since it has unclear stability and performance impact
# -------------------
# ELSEIF (OS_LINUX)
#    ALLOCATOR(TCMALLOC_256K)
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
