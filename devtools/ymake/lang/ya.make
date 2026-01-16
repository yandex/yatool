LIBRARY()

IF (CLANG OR GCC)
    CFLAGS(-Wno-unused-parameter)
ENDIF()

PEERDIR(
    library/cpp/string_utils/base64
    devtools/ymake/common
    devtools/ymake/diag
    devtools/ymake/lang/makelists
    devtools/ymake/options
    devtools/ymake/polexpr
    devtools/ymake/symbols
    devtools/ymake/yndex
    library/cpp/case_insensitive_string
    library/cpp/fieldcalc
    library/cpp/eventlog
    contrib/libs/antlr4_cpp_runtime
    contrib/libs/fmt
)

RUN_ANTLR4_CPP(
    TConf.g4
    -package
    NConfReader
    VISITOR
    OUTPUT_INCLUDES
    util/generic/string.h
    util/stream/output.h
)

RUN_ANTLR4_CPP_SPLIT(
    CmdLexer.g4
    CmdParser.g4
    VISITOR
)

SRCS(
    cmd_parser.cpp
    config_conditions.cpp
    confreader.cpp
    confreader_cache.cpp
    eval.cpp
    eval_context.cpp
    expansion.rl6
    macro_parser.rl6
    macro_values.cpp
    makefile_reader.cpp
    resolve_include.cpp
)

GENERATE_ENUM_SERIALIZATION(confreader_cache.h)
GENERATE_ENUM_SERIALIZATION(macro_values.h)

END()

RECURSE_FOR_TESTS(ut)
