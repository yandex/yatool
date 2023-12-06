#pragma once

#include "sem_graph.h"

#include <string>
#include <vector>
#include <filesystem>

namespace NYexport {

namespace fs = std::filesystem;

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

struct TBadYexportSpec: public std::runtime_error {
    TBadYexportSpec(const std::string& msg)
        : std::runtime_error{msg}
    {}
    TBadYexportSpec(std::string&& msg)
        : std::runtime_error{std::move(msg)}
    {}
};

}
