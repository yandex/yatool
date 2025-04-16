#pragma once

#include <util/generic/ylimits.h>
#include <util/system/types.h>

enum class EIncludesParserType: ui32 {
    EmptyParser = 0,
    CppOnlyParser,
    AsmParser,
    ProtoParser,
    LexParser,
    RagelParser,
    MapkitIdlParser,
    FortranParser,
    XsParser,
    XsynParser,
    SwigParser,
    CythonParser,
    FlatcParser,
    GoParser,
    ScParser,
    YDLParser,
    NlgParser,
    CfgprotoParser,
    TsParser,
    RosParser,
    RosTopicParser,
    PARSERS_COUNT,
    BAD_PARSER = Max<ui32>()
};
