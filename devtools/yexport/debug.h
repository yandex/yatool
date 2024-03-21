#pragma once

namespace NYexport {

    struct TDebugOpts {
        bool DebugSems = false;///< Dump input and patched sems to "dump_sems" attribute of each templates
        bool DebugAttrs = false;///< Dump attributes for template to "dump_attrs" attribute of each templates
    };

    /// Parse --debug-mode argument, return empty string on success, else text with error description
    std::string ParseDebugMode(const std::string& debugMode, TDebugOpts& debugOpts);
}
