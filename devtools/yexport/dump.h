#pragma once

#include <string>
#include <vector>

namespace NYexport {

    inline static const std::string DELIMETER = "|";
    inline static const std::string SEMS = "sems";
    inline static const std::string ATTRS = "attrs";

    struct TDumpOpts {
        bool DumpSems{false};///< Dump input and patched sems to stdout attribute of templates
        bool DumpAttrs{false};///< Dump attributes for template to stdout attribute of templates
        std::vector<std::string> DumpPathPrefixes;///< Path prefixes for dumps
    };

    /// Split some dump argument to list by delimeter |
    std::vector<std::string_view> ParseListInArg(const std::string& arg);

    /// Parse --dump-mode argument, return empty string on success, else text with error description
    std::string ParseDumpMode(const std::string& dumpMode, TDumpOpts& dumpOpts);

    /// Parse --dump-path-prefixes argument, return empty string on success, else text with error description
    std::string ParseDumpPathPrefixes(const std::string& dumpPathPrefixes, TDumpOpts& dumpOpts);

}
