#pragma once

#include "path_hash.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/serialized_enum.h>
#include <util/string/cast.h>
#include <contrib/libs/toml11/toml/value.hpp>
#include "generator_spec_enum.h"

#include <filesystem>
#include <string>
#include <vector>
#include <iosfwd>

namespace NYexport {

namespace fs = std::filesystem;

struct TTemplate{
    std::filesystem::path Template;
    std::string ResultName;

    bool operator== (const TTemplate&) const noexcept = default;
};

struct TTargetSpec {
    std::vector<TTemplate> Templates = {};
    THashSet<fs::path> Copy;

    bool operator==(const TTargetSpec&) const noexcept = default;
};

struct TAttrsSpecValue {
    EAttrTypes Type;
    THashSet<fs::path> Copy;

    bool operator== (const TAttrsSpecValue&) const noexcept = default;
};

struct TAttrsSpec {
    THashMap<std::string, TAttrsSpecValue> Items;

    bool operator== (const TAttrsSpec&) const noexcept = default;
};

inline const std::string ATTRGROUP_ROOT = "root";       // Root of all targets attribute
inline const std::string ATTRGROUP_TARGET = "target";   // Target for generator attribute
inline const std::string ATTRGROUP_INDUCED = "induced"; // Target for generator induced attribute (add to list for parent node in graph)

inline const std::string LIST_ITEM_TYPE = ".ITEM";      // Magic suffix for set list item type

struct TGeneratorSpec {
    TTargetSpec Root;
    THashMap<std::string, TTargetSpec> Targets;
    THashMap<std::string, TAttrsSpec> Attrs;
    THashMap<std::string, std::vector<std::filesystem::path>> Merge;
    bool UseManagedPeersClosure{false};
};

struct TBadGeneratorSpec: public std::runtime_error {
    TBadGeneratorSpec(const std::string& msg)
        : std::runtime_error{msg}
    {}
    TBadGeneratorSpec(std::string&& msg)
        : std::runtime_error{std::move(msg)}
    {}
};

enum class ESpecFeatures {
    All,
    CopyFilesOnly
};
using enum ESpecFeatures;

TGeneratorSpec ReadGeneratorSpec(const std::filesystem::path& path, ESpecFeatures features = All);
TGeneratorSpec ReadGeneratorSpec(std::istream& input, const std::filesystem::path& path, ESpecFeatures features = All);

}
