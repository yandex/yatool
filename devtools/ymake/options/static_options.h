#pragma once

#include <util/generic/strbuf.h>

/// @file some static constants

namespace NStaticConf {
    // static configuration
    static constexpr const unsigned INCLUDE_LINES_LIMIT = 60000;
    static constexpr const unsigned CALL_STACK_SIZE = 100;

    static constexpr const char* const ARRAY_SUFFIX = "...";
    static constexpr const char* const ARRAY_START = "[";
    static constexpr const char* const ARRAY_END = "]";

    static constexpr const char* const MODULE_INPUTS_MARKER = "GROUP_NAME=ModuleInputs";
    static constexpr const char* const INPUTS_MARKER = "GROUP_NAME=Inputs";
}
