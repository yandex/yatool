#include "dump.h"

namespace NYexport {

/// Split some dump argument to list by delimeter |
std::vector<std::string_view> ParseListInArg(const std::string& arg) {
    std::vector<std::string_view> r;
    if (!arg.empty()) {
        std::string_view arg_view{arg};
        size_t pos = 0;
        while (true) {
            auto newpos = arg_view.find(DELIMETER, pos);
            if (newpos == std::string::npos) {
                r.emplace_back(arg_view.substr(pos));
                break;
            } else {
                r.emplace_back(arg_view.substr(pos, newpos - pos));
                pos = newpos + DELIMETER.size();
            }
        }
    }
    return r;
}

/// Parse --dump-mode argument, return empty string on success, else text with error description
std::string ParseDumpMode(const std::string& dumpMode, TDumpOpts& dumpOpts) {
    auto values = ParseListInArg(dumpMode);
    for (auto value: values) {
        if (value == SEMS) {
            dumpOpts.DumpSems = true;
        } else if (value == ATTRS) {
            dumpOpts.DumpAttrs = true;
        } else {
            return "Unknown dump mode '" + std::string{value} + "'";
        }
    }
    return {};
}

/// Parse --dump-path-prefixes argument, return empty string on success, else text with error description
std::string ParseDumpPathPrefixes(const std::string& dumpPathPrefixes, TDumpOpts& dumpOpts) {
    auto values = ParseListInArg(dumpPathPrefixes);
    for (auto value: values) {
        dumpOpts.DumpPathPrefixes.emplace_back(value);
    }
    return {};
}

}
