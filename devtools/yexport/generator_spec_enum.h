#pragma once

namespace NYexport {

enum class EAttrTypes {
    Str /* "str" */,
    Bool /* "bool" */,
    Flag /* "flag" */,
    List /* "list" */,
    Set /* "set" */,
    SortedSet /* "sorted_set" */,
    Dict /* "dict" */,
    Unknown /* "unknown" */,
};

}
