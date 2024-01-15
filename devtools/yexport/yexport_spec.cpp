#include "yexport_spec.h"
#include "target_replacements.h"

#include <contrib/libs/toml11/toml/get.hpp>
#include <contrib/libs/toml11/toml/parser.hpp>
#include <contrib/libs/toml11/toml/value.hpp>

#include <util/string/builder.h>

#include <fstream>

namespace NYexport {

namespace NKeys {
    constexpr const char* TargetReplacements = "target_replacements"; ///< Array of target replacements
    constexpr const char* ReplacePathPrefixes = "replace_path_prefixes"; ///< Path prefixes for replacing
    constexpr const char* SkipPathPrefixes = "skip_path_prefixes"; ///< Path prefixes for skip replace
    constexpr const char* PathPrefix = "path_prefix"; ///< Path prefix for replace or skip
    constexpr const char* Excepts = "excepts"; ///< Excepts for replace or skip

    constexpr const char* Replacement = "replacement"; ///< String for replace sem data
    constexpr const char* Addition = "addition"; ///< String for addition to sem data
    constexpr const char* Name = "name"; ///< Name of sem
    constexpr const char* Args = "args"; ///< Args of sem
}

namespace {
    /// Get input stream by path, generate std::system_error if failed
    std::ifstream GetInputStream(const fs::path& path) {
        std::ifstream input{path};
        if (!input)
            throw std::system_error{errno, std::system_category(), "failed to open " + path.string()};
        return input;
    }

    /// Skip first and last spaces, slashes
    std::string_view SanitizePath(std::string_view path) {
        static constexpr const char* SPACES_AND_SEPARATORS = " \n\t\r/";

        auto first = path.find_first_not_of(SPACES_AND_SEPARATORS);
        if (first == std::string_view::npos) {
            return {};
        }
        auto last = path.find_last_not_of(SPACES_AND_SEPARATORS);
        if (last == std::string_view::npos) {
            return {};
        }
        return std::string_view(path.data() + first, last - first + 1);
    }

    /// Extract sanitized path from toml value
    TPathStr ParsePath(const toml::value& value) {
        auto path = TPathStr(SanitizePath(toml::get<std::string>(value)));
        if (path.empty()) {
            throw TBadYexportSpec({
                toml::format_error(
                    "[error] Empty path prefix",
                    value,
                    "All path prefixes must be non-empty"
                )
            });
        }
        return path;
    }

    /// Extract array of sanitized paths from toml value
    std::vector<TPathStr> ParsePathsArray(const toml::value& value) {
        auto tomlArray = toml::get<toml::array>(value);
        if (tomlArray.empty()) {
            return {};
        }
        std::vector<TPathStr> paths;
        paths.reserve(tomlArray.size());
        for (const auto& item : tomlArray) {
            paths.emplace_back(ParsePath(item));
        }
        return paths;
    }

    /// Extract path prefix spec from toml value
    TPathPrefixSpecs ParsePathPrefixes(const toml::value& value) {
        auto tomlArray = toml::get<toml::array>(value);
        if (tomlArray.empty()) {
            return {};
        }
        TPathPrefixSpecs pathPrefixes;
        pathPrefixes.reserve(tomlArray.size());
        for (const auto& item : tomlArray) {
            TPathStr path;
            std::vector<TPathStr> excepts;
            if (item.is_table()) {
                path = ParsePath(toml::find<toml::value>(item, NKeys::PathPrefix));
                auto tomlExcepts = toml::find_or<toml::value>(item, NKeys::Excepts, toml::array{});
                if (tomlExcepts.is_array()) {
                    excepts = ParsePathsArray(tomlExcepts);
                } else {
                    excepts.emplace_back(ParsePath(tomlExcepts));
                }
            } else {
                path = ParsePath(item);
            }
            if (!excepts.empty()) {
                TPathStr exceptBeg = path + '/';
                for (const auto& except: excepts) {
                    if (!except.starts_with(exceptBeg)) {
                        throw TBadYexportSpec({
                            toml::format_error(
                                "[error] Invalid except '" + except + "'",
                                item,
                                "Each except must begin by " + std::string(NKeys::PathPrefix) + " + /"
                            )
                        });
                    }
                }
            }
            pathPrefixes.emplace_back(TPathPrefixSpec{ .Path = std::move(path), .Excepts = std::move(excepts) });
        }
        return pathPrefixes;
    }

    /// Extract one node semantic (name, args...) from toml value
    TNodeSemantic ParseNodeSemantic(const toml::value& value) {
        TNodeSemantic sem;
        if (value.is_string()) {
            sem.emplace_back(toml::get<std::string>(value));
        } else {
            std::string name = toml::get<std::string>(toml::find<toml::value>(value, NKeys::Name));
            const auto& tomlArgs = toml::get<toml::array>(toml::find_or<toml::value>(value, NKeys::Args, toml::array{}));
            sem.reserve(1 /*name*/ + tomlArgs.size());
            sem.emplace_back(std::move(name));
            for (const auto& tomlArg : tomlArgs) {
                sem.emplace_back(toml::get<std::string>(tomlArg));
            }
        }
        if (sem[0].empty()) {
            throw TBadYexportSpec({
                toml::format_error(
                    "[error] Empty name of semantic",
                    value,
                    "Each semantic must has non-empty name and optional args with any values"
                )
            });
        }
        return sem;
    }

    /// Extract array of node semantics from toml value
    TNodeSemantics ParseNodeSemantics(const toml::value& value) {
        const auto& tomlSems = toml::get<toml::array>(value);
        if (tomlSems.empty()) {
            return {};
        }
        TNodeSemantics sems;
        sems.reserve(tomlSems.size());
        for (const auto& tomlSem : tomlSems) {
            sems.emplace_back(ParseNodeSemantic(tomlSem));
        }
        return sems;
    }

    /// Extract one target replacement from toml value
    TTargetReplacementSpec ParseTargetReplacementSpec(const toml::value& value, const char* name) {
        TTargetReplacementSpec spec;
        spec.ReplacePathPrefixes = ParsePathPrefixes(toml::find_or<toml::value>(value, NKeys::ReplacePathPrefixes, toml::array{}));
        spec.SkipPathPrefixes = ParsePathPrefixes(toml::find_or<toml::value>(value, NKeys::SkipPathPrefixes, toml::array{}));
        if ((spec.ReplacePathPrefixes.empty() && spec.SkipPathPrefixes.empty())
            || (!spec.ReplacePathPrefixes.empty() && !spec.SkipPathPrefixes.empty()))
        {
            throw TBadYexportSpec({
                toml::format_error(
                    "[error] Invalid item in " + std::string(name),
                    value,
                    "Each replacement item must contains " + std::string(NKeys::ReplacePathPrefixes) + " strong or " + std::string(NKeys::SkipPathPrefixes) + " values"
                )
            });
        }
        spec.Replacement = ParseNodeSemantics(toml::find_or<toml::value>(value, NKeys::Replacement, toml::array{}));
        spec.Addition = ParseNodeSemantics(toml::find_or<toml::value>(value, NKeys::Addition, toml::array{}));
        if ((spec.Replacement.empty() && spec.Addition.empty())
            || (!spec.Replacement.empty() && !spec.Addition.empty())) {
            throw TBadYexportSpec({
                toml::format_error(
                    "[error] Invalid item in " + std::string(name),
                    value,
                    "Each replacement item must contains " + std::string(NKeys::Replacement) + " strong or " + std::string(NKeys::Addition) + " value"
                )
            });
        }
        return spec;
    }

    jinja2::ValuesList ParseArray(const toml::array& array);
    jinja2::ValuesMap ParseTable(const toml::table& table);

    jinja2::Value ParseValue(const toml::value& value) {
        if (value.is_table()) {
            return ParseTable(value.as_table());
        } else if (value.is_array()) {
            return ParseArray(value.as_array());
        } else if (value.is_string()) {
            return value.as_string();
        } else if (value.is_floating()) {
            return value.as_floating();
        } else if (value.is_integer()) {
            return value.as_integer();
        } else if (value.is_boolean()) {
            return value.as_boolean();
        } else if (value.is_local_date() || value.is_local_datetime() || value.is_offset_datetime()) {
            return value.as_string();
        } else {
            return {};
        }
    }

    jinja2::ValuesMap ParseTable(const toml::table& table) {
        jinja2::ValuesMap map;
        for (const auto& [key, value] : table) {
            map.emplace(key, ParseValue(value));
        }
        return map;
    }

    jinja2::ValuesList ParseArray(const toml::array& array) {
        jinja2::ValuesList list;
        for (const auto& value : array) {
            list.emplace_back(ParseValue(value));
        }
        return list;
    }
}

/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(const fs::path& path, TTargetReplacements& targetReplacements) {
    auto ifstream = GetInputStream(path);
    LoadTargetReplacements(ifstream, path, targetReplacements);
}

/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(std::istream& input, const fs::path& path, TTargetReplacements& targetReplacements) {
    try {
        const auto doc = toml::parse(input, path.string());
        const auto& value = toml::get<toml::array>(find_or<toml::value>(doc, NKeys::TargetReplacements, toml::array{}));
        for (const auto& item : value) {
            auto spec = ParseTargetReplacementSpec(item, NKeys::TargetReplacements);
            if (targetReplacements.ValidateSpec(spec, [&](TStringBuf errors) {
                throw TBadYexportSpec({
                    toml::format_error(
                        "[error] Invalid replacement: " + std::string(errors),
                        item,
                        "Replace and skip path prefixes must be unique and only one global replacement (without replace path prefixes) allowed."
                    )
                });
            })) {
                targetReplacements.AddSpec(spec);
            }
        }
    } catch (const toml::exception& err) {
        throw TBadYexportSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadYexportSpec{err.what()};
    }
}

TYexportSpec ReadYexportSpec(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input)
        throw std::system_error{errno, std::system_category(), "failed to open " + path.string()};
    return ReadYexportSpec(input, path);
}

TYexportSpec ReadYexportSpec(std::istream& input, const std::filesystem::path& path) {
    try {
        const auto doc = toml::parse(input, path.string());
        TYexportSpec spec;
        const auto& addattrsSection = find_or<toml::value>(doc, YEXPORT_ADD_ATTRS, toml::table{});
        if (!addattrsSection.is_table()) {
            throw TBadYexportSpec({
                toml::format_error(
                    "[error] Invalid format of " + std::string(YEXPORT_ADD_ATTRS),
                    addattrsSection,
                    std::string(YEXPORT_ADD_ATTRS) + " must be table with optional " + std::string(YEXPORT_ADDATTRS_DIR) + " and " + std::string(YEXPORT_ADDATTRS_TARGET) + " values"
                )
            });
        }
        const auto& addattrsDir = find_or<toml::value>(addattrsSection, YEXPORT_ADDATTRS_DIR, toml::table{});
        spec.AddAttrsDir = ParseTable(addattrsDir.as_table());
        const auto& addattrsTarget = find_or<toml::value>(addattrsSection, YEXPORT_ADDATTRS_TARGET, toml::table{});
        spec.AddAttrsTarget = ParseTable(addattrsTarget.as_table());
        return spec;
    } catch (const toml::exception& err) {
        throw TBadYexportSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadYexportSpec{err.what()};
    }
}

void TYexportSpec::Dump(IOutputStream& out) const {
    jinja2::ValuesMap map;
    map.emplace(YEXPORT_ADD_ATTRS + "." + YEXPORT_ADDATTRS_DIR, AddAttrsDir);
    map.emplace(YEXPORT_ADD_ATTRS + "." + YEXPORT_ADDATTRS_TARGET, AddAttrsTarget);
    ::NYexport::Dump(out, map);
}

std::string TYexportSpec::Dump() const {
    TStringBuilder dump;
    Dump(dump.Out);
    return dump;
}

}
