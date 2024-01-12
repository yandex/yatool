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
void LoadTargetReplacements(const fs::path& path, TTargetReplacements& targetReplacements);
/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(std::istream& input, const fs::path& path, TTargetReplacements& targetReplacements);

inline const std::string YEXPORT_HANDLER = "handler"; ///< Section in yexport.toml for direct transit to generator
struct TYexportSpec {
    jinja2::ValuesMap Handler; ///< Attributes from handler for direct transit to generator

    void Dump(IOutputStream& out) const;
    std::string Dump() const;
};

inline bool operator== (const TYexportSpec& a, const TYexportSpec& b) noexcept {
    return a.Dump() == b.Dump();
};

TYexportSpec ReadYexportSpec(const std::filesystem::path& path);
TYexportSpec ReadYexportSpec(std::istream& input, const std::filesystem::path& path);

struct TBadYexportSpec: public std::runtime_error {
    TBadYexportSpec(const std::string& msg)
        : std::runtime_error{msg}
    {}
    TBadYexportSpec(std::string&& msg)
        : std::runtime_error{std::move(msg)}
    {}
};

}
