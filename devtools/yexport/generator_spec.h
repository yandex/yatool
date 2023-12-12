#pragma once

#include "path_hash.h"
#include "diag/exception.h"

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

struct TGeneratorRule {
    THashSet<std::string> Attributes;
    THashSet<fs::path> Copy;
    THashMap<std::string, TVector<std::string>> AddValues; //TODO: Proper values instead of std::string

    bool operator== (const TGeneratorRule&) const noexcept = default;
};


inline const std::string ATTRGROUP_ROOT = "root";       // Root of all targets attribute
inline const std::string ATTRGROUP_TARGET = "target";   // Target for generator attribute
inline const std::string ATTRGROUP_INDUCED = "induced"; // Target for generator induced attribute (add to list for parent node in graph)

inline const std::string LIST_ITEM_TYPE = "-ITEM";      // Magic suffix for set list item type
inline const char ATTR_DIVIDER = '-';                   // Divider char for tree of attributes

struct TGeneratorSpec {
    using TRuleSet = THashSet<const TGeneratorRule*>;

    TTargetSpec Root;
    THashMap<std::string, TTargetSpec> Targets;
    THashMap<std::string, TAttrsSpec> Attrs;
    THashMap<std::string, std::vector<std::filesystem::path>> Merge;
    THashMap<uint32_t, TGeneratorRule> Rules;
    THashMap<std::string, TVector<uint32_t>> AttrToRuleId;
    bool UseManagedPeersClosure{false};

    TRuleSet GetRules(const std::string attr) const;
};

struct TBadGeneratorSpec: public TYExportException {
    TBadGeneratorSpec(const std::string& msg)
        : TYExportException{msg}
    {}
    TBadGeneratorSpec(std::string&& msg)
        : TYExportException{std::move(msg)}
    {}
    TBadGeneratorSpec() = default;
};

namespace NGeneratorSpecError {
    constexpr const char* WrongFieldType = "Wrong field type";
    constexpr const char* MissingField = "Missing field";
    constexpr const char* SpecificationError = "Specification error";
}

enum class ESpecFeatures {
    All,
    CopyFilesOnly
};
using enum ESpecFeatures;

TGeneratorSpec ReadGeneratorSpec(const std::filesystem::path& path, ESpecFeatures features = All);
TGeneratorSpec ReadGeneratorSpec(std::istream& input, const std::filesystem::path& path, ESpecFeatures features = All);

}
