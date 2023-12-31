#pragma once

#include "flat_attribute.h"
#include "std_helpers.h"
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

enum class ECopyLocation {
    GeneratorRoot = 0,
    SourceRoot,
};

struct TCopySpec {
    THashMap<ECopyLocation, THashSet<fs::path>> Items;

    bool Useless() const;
    void Append(const TCopySpec& copySpec);

    bool operator== (const TCopySpec&) const noexcept = default;
};


struct TTemplate {
    fs::path Template;
    std::string ResultName;

    bool operator== (const TTemplate&) const noexcept = default;
};

struct TTargetSpec {
    std::vector<TTemplate> Templates = {};
    TCopySpec Copy;

    bool operator==(const TTargetSpec&) const noexcept = default;
};

struct TAttrsSpecValue {
    EAttrTypes Type;

    bool operator== (const TAttrsSpecValue&) const noexcept = default;
};

struct TAttrsSpec {
    THashMap<std::string, TAttrsSpecValue> Items;

    bool operator== (const TAttrsSpec&) const noexcept = default;
};

struct TGeneratorRule {
    TCopySpec Copy;
    THashSet<std::string> Attributes;
    THashMap<std::string, TVector<std::string>> AddValues; //TODO: Proper values instead of std::string

    bool Useless() const;
    bool operator== (const TGeneratorRule&) const noexcept = default;
};


inline const std::string ATTRGROUP_ROOT = "root";       // Root of all targets attribute
inline const std::string ATTRGROUP_TARGET = "target";   // Target for generator attribute
inline const std::string ATTRGROUP_INDUCED = "induced"; // Target for generator induced attribute (add to list for parent node in graph)

struct TGeneratorSpec {
    using TRuleSet = THashSet<const TGeneratorRule*>;

    TTargetSpec Root;
    THashMap<std::string, TTargetSpec> Targets;
    THashMap<std::string, TAttrsSpec> Attrs;
    THashMap<std::string, TVector<uint32_t>> AttrToRuleId;
    THashMap<std::string, TVector<fs::path>> Merge;
    THashMap<uint32_t, TGeneratorRule> Rules;
    bool UseManagedPeersClosure{false};

    TRuleSet GetRules(std::string_view attr) const;
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
    constexpr const char* UnknownField = "Unknown field";
}

enum class ESpecFeatures {
    All,
    CopyFilesOnly
};
using enum ESpecFeatures;

TGeneratorSpec ReadGeneratorSpec(const fs::path& path, ESpecFeatures features = All);
TGeneratorSpec ReadGeneratorSpec(std::istream& input, const fs::path& path, ESpecFeatures features = All);

}
