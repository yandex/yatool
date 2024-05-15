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
    Skip /* "skip" */,
    Unknown /* "unknown" */,
};

enum class ECopyLocation {
    GeneratorRoot = 0,
    SourceRoot,
};

enum class EAttrGroup {
    Unknown = 0,
    Root,      // Root of export
    Platform,  // Current platform
    Directory, // Current directory
    Target,    // Current target
    Induced,   // Childs of current target
};

}
