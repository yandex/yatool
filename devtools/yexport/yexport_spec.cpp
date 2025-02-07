#include "yexport_spec.h"
#include "target_replacements.h"

#include <contrib/libs/toml11/include/toml11/types.hpp>
#include <contrib/libs/toml11/include/toml11/get.hpp>
#include <contrib/libs/toml11/include/toml11/parser.hpp>
#include <contrib/libs/toml11/include/toml11/find.hpp>

#include <util/string/builder.h>

#include <spdlog/spdlog.h>

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

    constexpr const char* DefaultGenerator = "default_generator"; ///< Name of default generator
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
    std::vector<TPathStr> ParsePathsArray(const toml::array& tomlArray) {
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
    TPathPrefixSpecs ParsePathPrefixes(const toml::array& tomlArray) {
        if (tomlArray.empty()) {
            return {};
        }
        TPathPrefixSpecs pathPrefixes;
        pathPrefixes.reserve(tomlArray.size());
        for (const auto& item : tomlArray) {
            TPathStr path;
            std::vector<TPathStr> excepts;
            if (item.is_table()) {
                path = ParsePath(item.at(NKeys::PathPrefix));
                if (item.contains(NKeys::Excepts)) {
                    auto& tomlExcepts = item.at(NKeys::Excepts);
                    if (tomlExcepts.is_array()) {
                        excepts = ParsePathsArray(tomlExcepts.as_array());
                    } else if (tomlExcepts.is_string()) {
                        excepts.emplace_back(ParsePath(tomlExcepts));
                    }
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
            auto name = toml::find<std::string>(value, NKeys::Name);
            const auto& tomlArgs = value.contains(NKeys::Args)
                ? toml::find<toml::array>(value, NKeys::Args)
                : toml::array{};
            sem.reserve(1 /*name*/ + tomlArgs.size());
            sem.emplace_back(std::move(name));
            if (!tomlArgs.empty()) {
                for (const auto& tomlArg : tomlArgs) {
                    sem.emplace_back(toml::get<std::string>(tomlArg));
                }
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
    TNodeSemantics ParseNodeSemantics(const toml::array& tomlSems) {
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
        if (value.contains(NKeys::ReplacePathPrefixes)) {
            spec.ReplacePathPrefixes = ParsePathPrefixes(toml::find<toml::array>(value, NKeys::ReplacePathPrefixes));
        }
        if (value.contains(NKeys::SkipPathPrefixes)) {
            spec.SkipPathPrefixes = ParsePathPrefixes(toml::find<toml::array>(value, NKeys::SkipPathPrefixes));
        }
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
        if (value.contains(NKeys::Replacement)) {
            spec.Replacement = ParseNodeSemantics(toml::find<toml::array>(value, NKeys::Replacement));
        }
        if (value.contains(NKeys::Addition)) {
            spec.Addition = ParseNodeSemantics(toml::find<toml::array>(value, NKeys::Addition));
        }
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

}

/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(const fs::path& yexportTomlPath, TTargetReplacements& targetReplacements) {
    auto ifstream = GetInputStream(yexportTomlPath);
    LoadTargetReplacements(ifstream, yexportTomlPath, targetReplacements);
}

/// Parse toml and load step by step it (after validation) to targetReplacements
void LoadTargetReplacements(std::istream& input, const fs::path& yexportTomlPath, TTargetReplacements& targetReplacements) {
    try {
        const auto doc = toml::parse(input, yexportTomlPath.string());
        if (doc.contains(NKeys::TargetReplacements)) {
            const auto& value = find<toml::array>(doc, NKeys::TargetReplacements);
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
        }
    } catch (const toml::exception& err) {
        throw TBadYexportSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadYexportSpec{err.what()};
    }
}

TYexportSpec ReadYexportSpec(const std::filesystem::path& yexportTomlPath) {
    std::ifstream input{yexportTomlPath};
    if (!input)
        throw std::system_error{errno, std::system_category(), "failed to open " + yexportTomlPath.string()};
    return ReadYexportSpec(input, yexportTomlPath);
}

TYexportSpec ReadYexportSpec(std::istream& input, const std::filesystem::path& yexportTomlPath) {
    try {
        const auto doc = toml::parse(input, yexportTomlPath.string());
        TYexportSpec spec;
        if (doc.contains(YEXPORT_ADD_ATTRS)) {
            const auto& addattrsSection = doc.at(YEXPORT_ADD_ATTRS);
            if (!addattrsSection.is_table()) {
                throw TBadYexportSpec({
                    toml::format_error(
                        "[error] Invalid format of " + std::string(YEXPORT_ADD_ATTRS),
                        addattrsSection,
                        std::string(YEXPORT_ADD_ATTRS) + " must be table with optional " + std::string(YEXPORT_ADDATTRS_ROOT) + ", " + std::string(YEXPORT_ADDATTRS_DIR) + " or " + std::string(YEXPORT_ADDATTRS_TARGET) + " values"
                    )
                });
            }
            if (addattrsSection.contains(YEXPORT_ADDATTRS_ROOT)) {
                spec.AddAttrsRoot = ParseTable(toml::find<toml::table>(addattrsSection, YEXPORT_ADDATTRS_ROOT));
            }
            if (addattrsSection.contains(YEXPORT_ADDATTRS_DIR)) {
                spec.AddAttrsDir = ParseTable(toml::find<toml::table>(addattrsSection, YEXPORT_ADDATTRS_DIR));
            }
            if (addattrsSection.contains(YEXPORT_ADDATTRS_TARGET)) {
                spec.AddAttrsTarget = ParseTable(toml::find<toml::table>(addattrsSection, YEXPORT_ADDATTRS_TARGET));
            }
        }
        return spec;
    } catch (const toml::exception& err) {
        throw TBadYexportSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadYexportSpec{err.what()};
    }
}

std::string GetDefaultGenerator(const fs::path& yexportTomlPath) {
    auto spath = yexportTomlPath.string();
    if (fs::exists(yexportTomlPath)) {
        try {
            std::ifstream input{yexportTomlPath};
            if (!input) {
                throw std::system_error{errno, std::system_category(), "failed to open " + spath};
            }
            const auto doc = toml::parse(input, spath);
            if (!doc.contains(NKeys::DefaultGenerator)) {
                return {};
            }
            return toml::get<std::string>(find<toml::value>(doc, NKeys::DefaultGenerator));
        } catch (const std::exception& err) {
            spdlog::error("Can't detect " + std::string{NKeys::DefaultGenerator} + " from " + spath + ": " + err.what());
        }
    }
    return {};
}

void TYexportSpec::Dump(IOutputStream& out) const {
    jinja2::ValuesMap map;
    map.emplace(YEXPORT_ADD_ATTRS + "." + YEXPORT_ADDATTRS_ROOT, AddAttrsRoot);
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
