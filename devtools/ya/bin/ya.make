PY3_PROGRAM(ya-bin)

SPLIT_DWARF()

IF (OS_LINUX)
    IF (MUSL)
        ALLOCATOR(J)
    ELSE()
        ALLOCATOR(SYSTEM)
    ENDIF()
ENDIF()

PY_MAIN(entry.main)

PEERDIR(
    contrib/deprecated/python/ujson
    devtools/ya/cpp/entry
    devtools/ya/cpp/lib/start_stager
    devtools/ya/entry
    devtools/ya/handlers
)

IF (NOT YA_OPENSOURCE)
    PEERDIR(
        library/python/svn_version
    )
ENDIF()

CHECK_DEPENDENT_DIRS(
    DENY
    contrib/python/lxml
)

ENABLE(NO_STRIP)

# https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation#enable-long-paths-in-windows-10-version-1607-and-later
WINDOWS_LONG_PATH_MANIFEST()

# Comment for trigger pr
END()
