#include "dump.h"
#include "debug.h"

#include <sstream>

namespace NYexport {

/// Parse --debug-mode argument, return empty string on success, else text with error description
std::string ParseDebugMode(const std::string& debugMode, TDebugOpts& debugOpts) {
    auto values = ParseListInArg(debugMode);
    for (auto value: values) {
        if (value == SEMS) {
            debugOpts.DebugSems = true;
        } else if (value == ATTRS) {
            debugOpts.DebugAttrs = true;
        } else {
            return "Unknown debug mode '" + std::string{value} + "' " + std::to_string(value.size());
        }
    }
    return {};
}

}
