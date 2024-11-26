#pragma once

#include "sem_graph.h"
#include "std_helpers.h"
#include "jinja_helpers.h"

#include <string>
#include <vector>
#include <filesystem>

namespace NYexport {

using TPathStr = std::string;

struct TPathPrefixSpec {
    TPathStr Path;
    std::vector<TPathStr> Excepts;
};
using TPathPrefixSpecs = std::vector<TPathPrefixSpec>;

struct TTargetReplacementSpec {
    TPathPrefixSpecs ReplacePathPrefixes;
    TPathPrefixSpecs SkipPathPrefixes;
    TNodeSemantics Replacement;
    TNodeSemantics Addition;
};
using TTargetReplacementSpecs = std::vector<TTargetReplacementSpec>;

class TTargetReplacements;
/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(const fs::path& yexportTomlPath, TTargetReplacements& targetReplacements);
/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(std::istream& input, const fs::path& yexportTomlPath, TTargetReplacements& targetReplacements);

inline const std::string YEXPORT_ADD_ATTRS = "add_attrs"; ///< Section in yexport.toml for direct transit attributes to generator
inline const std::string YEXPORT_ADDATTRS_ROOT = "root"; ///< Direct transit attributes for root
inline const std::string YEXPORT_ADDATTRS_DIR = "dir"; ///< Direct transit attributes for directories
inline const std::string YEXPORT_ADDATTRS_TARGET = "target"; ///< Direct transit attributes for targets
struct TYexportSpec {
    jinja2::ValuesMap AddAttrsRoot; ///< Attributes from yexport.toml for direct transit to root
    jinja2::ValuesMap AddAttrsDir; ///< Attributes from yexport.toml for direct transit to directory
    jinja2::ValuesMap AddAttrsTarget; ///< Attributes from yexport.toml for direct transit to target

    void Dump(IOutputStream& out) const;
    std::string Dump() const;
};

inline bool operator== (const TYexportSpec& a, const TYexportSpec& b) noexcept {
    return a.Dump() == b.Dump();
};

TYexportSpec ReadYexportSpec(const fs::path& yexportTomlPath);
TYexportSpec ReadYexportSpec(std::istream& input, const fs::path& yexportTomlPath);
std::string GetDefaultGenerator(const fs::path& yexportTomlPath);

struct TBadYexportSpec: public std::runtime_error {
    TBadYexportSpec(const std::string& msg)
        : std::runtime_error{msg}
    {}
    TBadYexportSpec(std::string&& msg)
        : std::runtime_error{std::move(msg)}
    {}
};

}
