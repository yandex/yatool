#pragma once

#include <util/generic/strbuf.h>

/// @file some static constants

namespace NStaticConf {
    // static configuration
    static const unsigned INCLUDE_LINES_LIMIT = 60000;
    static const unsigned CALL_STACK_SIZE = 100;

    static const char* ARRAY_SUFFIX = "...";
    static const char* ARRAY_START = "[";
    static const char* ARRAY_END = "]";

    static const char* PACKAGE_OUTPUTS_MARKER = "GROUP_NAME=PackageOutputs";
    static const char* MODULE_INPUTS_MARKER = "GROUP_NAME=ModuleInputs";
    static const char* INPUTS_MARKER = "GROUP_NAME=Inputs";
}
