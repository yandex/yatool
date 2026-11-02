LIBRARY()

IF (CLANG OR GCC)
    CFLAGS(-Wno-unused-parameter)
ENDIF()

PEERDIR(
    library/cpp/string_utils/base64
    devtools/ymake/common
    devtools/ymake/diag
    devtools/ymake/lang/makelists
    devtools/ymake/libs/clocks
    devtools/ymake/options
    devtools/ymake/libs/polexpr
    devtools/ymake/symbols
    devtools/ymake/yndex
    library/cpp/case_insensitive_string
    library/cpp/fieldcalc
    library/cpp/eventlog
    contrib/libs/fmt
)

SRCS(
    cmd_parser.cpp
    parser_lexer.rl6
    call_args_parser.cpp
    call_signature.cpp
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
    value_storage.cpp
)

GENERATE_ENUM_SERIALIZATION(call_args_parser.h)
GENERATE_ENUM_SERIALIZATION(confreader_cache.h)
GENERATE_ENUM_SERIALIZATION(macro_values.h)

END()

RECURSE_FOR_TESTS(ut)
