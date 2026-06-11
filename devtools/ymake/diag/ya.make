LIBRARY()

PEERDIR(
    contrib/libs/fmt
    library/cpp/json
    library/cpp/protobuf/json
    devtools/ymake/common
    devtools/ymake/diag/common_display
    devtools/ymake/diag/common_msg
    devtools/ymake/libs/clocks
    devtools/ymake/libs/name_store
)

SRCS(
    dbg.cpp
    debug_log_writer.cpp
    diag.cpp
    display.cpp
    manager.cpp
    mod_stats_manager.cpp
    progress_manager.cpp
    stats.cpp
    trace.ev
    trace.cpp
)

GENERATE_ENUM_SERIALIZATION_WITH_HEADER(
    trace_type_enums.h
)

GENERATE_ENUM_SERIALIZATION_WITH_HEADER(
    stats_enums.h
)

IF(YMAKE_DEBUG)
    CFLAGS(GLOBAL -DYMAKE_DEBUG)
ENDIF()

IF (OS_WINDOWS)
    CFLAGS(
        GLOBAL -DASIO_WINDOWS_APP
    )
ENDIF()

END()

RECURSE_FOR_TESTS(ut)
