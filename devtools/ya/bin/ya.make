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

IF (AUTOCHECK)
    SET(AUTOCHECK_TOOL_LIST
        ag  # devtools/ya/handlers/grep/tests/acceptance, devtools/ya/handlers/tool/tests/acceptance
        arc  # devtools/autocheck/libs/platform_miner/tests/large
        black  # devtools/ya/cpp/entry/tests
        c++
        clang-format  # devtools/ya/handlers/style/tests/acceptance
        jstyle  # devtools/ya/handlers/jstyle/tests/acceptance
        print_args  # devtools/ya/yalibrary/tools/tests/acceptance
        python
        python3
        run_python_udf  # devtools/ya/handlers/nile/tests/acceptance
        tofu  # devtools/ya/handlers/style/tests/acceptance
        ya-tc
        yamlfmt  # devtools/ya/handlers/style/tests/acceptance
        ymake  # yabs/server/cs/internal_ver/cs_import/test
        yoimports  # devtools/ya/handlers/style/tests/acceptance
        yql-format  # devtools/ya/handlers/style/tests/acceptance
    )
    INCLUDE(${ARCADIA_ROOT}/devtools/ya/conf/ya.make.inc)
ENDIF()

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
    contrib/ydb
    kikimr
)

ENABLE(NO_STRIP)

# https://docs.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation#enable-long-paths-in-windows-10-version-1607-and-later
WINDOWS_LONG_PATH_MANIFEST()

# Comment for trigger pr
END()
