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

enum class ECopyLocation {
    GeneratorRoot = 0,
    SourceRoot,
};

enum class EAttrGroup {
    Unknown = 0,
    Root,      // Root of all targets attribute
    Target,    // Target for generator attribute
    Induced,   // Target for generator induced attribute (add to list for parent node in graph)
    Directory, // Attributes of subdirectory
};

}
