LIBRARY()

SRCS(
    asm_parser.cpp
    base.cpp
    cfgproto_parser.cpp
    cpp_includes_parser.rl6
    cpp_parser.cpp
    cython_parser.rl6
    empty_parser.cpp
    flatc_parser.cpp
    fortran_parser.cpp
    go_import_parser.rl6
    go_parser.cpp
    lex_parser.cpp
    mapkit_idl_parser.cpp
    nlg_parser.cpp
    nlg_includes_parser.rl6
    proto_parser.cpp
    ragel_parser.cpp
    ros_parser.cpp
    ros_topic_parser.cpp
    sc_parser.cpp
    swig_parser.cpp
    xs_parser.cpp
    ydl_imports_parser.rl6
    ydl_parser.cpp
    ts_import_parser.rl6
    ts_parser.cpp
)

PEERDIR(
    devtools/ymake/diag
    devtools/ymake/options
    devtools/ymake/symbols
    library/cpp/regex/pcre
)

IF (OPENSOURCE)
    SRCS(
        dummy_xsyn_parser.cpp
    )
ELSE()
    SRCS(
        xsyn_parser.cpp
    )

    PEERDIR(
        library/cpp/xml/document
    )
ENDIF()

END()

RECURSE(
    ut
)
