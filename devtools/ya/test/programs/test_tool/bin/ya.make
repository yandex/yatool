PY3_PROGRAM(test_tool)

PY_SRCS(
    __main__.py
)

PEERDIR(
    devtools/ya/test/programs/test_tool/main_entry_point
    library/cpp/testing/startup_timestamp
)

WINDOWS_LONG_PATH_MANIFEST()

IF (AUTOCHECK)
    SET(AUTOCHECK_TOOL_LIST
        arc
    )
    INCLUDE(${ARCADIA_ROOT}/devtools/ya/conf/ya.make.inc)
ENDIF()


END()
