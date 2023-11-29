#include "generator_spec.h"

#include <contrib/libs/toml11/toml/get.hpp>
#include <contrib/libs/toml11/toml/parser.hpp>
#include <contrib/libs/toml11/toml/value.hpp>

#include <fstream>

using namespace std::literals;


namespace NKeys {
    constexpr const char* Root = "root";
    constexpr const char* Template = "template";
    constexpr const char* ManyTemplates = "templates";
    constexpr const char* Path = "path";
    constexpr const char* Dest = "dest";
    constexpr const char* Copy = "copy";
    constexpr const char* Targets = "targets";
    constexpr const char* Attrs = "attrs";
    constexpr const char* Merge = "merge";
    constexpr const char* Type = "type";
    constexpr const char* Rules = "rules";
    constexpr const char* UseManagedPeersClosure = "use_managed_peers_closure";
}

namespace {
    void ParseOneTemplate(TTargetSpec& targetSpec, const toml::value& tmpl) {
        if (tmpl.is_string()) {
            TTemplate templateResult;

            templateResult.Template = toml::get<std::string>(tmpl);

            if (templateResult.Template.extension() != ".jinja") {
                throw TBadGeneratorSpec{
                    toml::format_error(
                        "[error] Can't deduce destination filename from template path",
                        tmpl,
                        "template must either have '.jinja' extension or {path='...', "
                        "dest='...'} form should be used"
                    )
                };
            }

            templateResult.ResultName = templateResult.Template.filename().replace_extension().string();
            targetSpec.Templates.push_back(templateResult);
        } else if (tmpl.is_table()) {
            targetSpec.Templates.push_back({
                    .Template = toml::find<std::string>(tmpl, NKeys::Path),
                    .ResultName = toml::find<std::string>(tmpl, NKeys::Dest)
                });
        } else {
            throw TBadGeneratorSpec{
                toml::format_error("[error] invalid template value", tmpl,
                                "either path with '.jinja' extension or table "
                                "{path='...', dest='...'} expected")};
        }
    }

    void ParseManyTemplates(TTargetSpec& targetSpec, const toml::value& tmplArray) {
        if (tmplArray.is_array()) {
            for (const toml::value& tmpl : toml::get<std::vector<toml::value>>(tmplArray)) {
                ParseOneTemplate(targetSpec, tmpl);
            }
        } else {
            throw TBadGeneratorSpec{
                toml::format_error("[error] invalid templates value", tmplArray,
                                "must be array of templates: either path with '.jinja' extension or table "
                                "{path='...', dest='...'} expected")};
        }
    }

    void ParseTemplates(TTargetSpec& targetSpec, const toml::value& target) {
        bool containsOne = target.contains(NKeys::Template);
        bool containsMany = target.contains(NKeys::ManyTemplates);

        if (!(containsOne ^ containsMany)) {
            throw TBadGeneratorSpec{
                toml::format_error(
                    "[error] invalid attrubutes",
                    target,
                    "must contains template = {path='...', dest='...'} "
                    "or "
                    "templates = [{path='...', dest='...'}, {path='...', dest='...'}] "
                    "but not together"
                )
            };
        }

        if (containsMany) {
            ParseManyTemplates(targetSpec, toml::find(target, NKeys::ManyTemplates));
        } else {
            ParseOneTemplate(targetSpec, toml::find(target, NKeys::Template));
        }
    }

    TTargetSpec ParseTargetSpec(const toml::value& target, ESpecFeatures features) {
        TTargetSpec targetSpec;
        if (features != CopyFilesOnly) {
            ParseTemplates(targetSpec, target);
        }

        for (const auto& item : toml::find_or<toml::array>(target, NKeys::Copy, toml::array{})) {
            targetSpec.Copy.insert(toml::get<std::string>(item));
        }
        return targetSpec;
    }

    TAttrsSpec ParseAttrsSpec(const toml::value& attrs) {
        TAttrsSpec attrsSpec;
        for (const auto& item : attrs.as_table()) {
            EAttrTypes value;
            if (item.second.is_table()) {
                const auto& table = item.second.as_table();
                if (table.contains(NKeys::Copy)) {
                    if (TryFromString(table.find(NKeys::Type)->second.as_string(), value)) {
                        attrsSpec.Items[item.first].Type = value;
                    }
                    const auto& array = table.find(NKeys::Copy)->second.as_array();
                    for (const auto& arrayItem : array) {
                        if (arrayItem.is_string()) {
                            attrsSpec.Items[item.first].Copy.insert(fs::path{std::string(arrayItem.as_string())});
                        }
                    }
                }
            } else if (TryFromString(item.second.as_string(), value)) {
                attrsSpec.Items[item.first].Type = value;
            } else {
                throw TBadGeneratorSpec(toml::format_error("[error] invalid attrs value", toml::find(attrs, item.first),
                                                           " value must be one of the following data types: " + GetEnumAllNames<EAttrTypes>()));
            }
        }
        return attrsSpec;
    }

    void ParseRules(const toml::value& value, THashMap<std::string, TAttrsSpec>& attrs) {
        const auto& table = value.as_table();
        if (!table.contains(NKeys::Attrs)) {
            return;
        }
        if (!table.contains(NKeys::Copy)) {
            return;
        }
        THashSet<fs::path> filesToCopy;
        for (const auto& file : table.at(NKeys::Copy).as_array()) {
            filesToCopy.insert(fs::path(file.as_string().str));
        }
        for (const auto& attr : table.at(NKeys::Attrs).as_array()) {
            attrs["target"].Items[attr.as_string().str].Copy.insert(filesToCopy.begin(), filesToCopy.end());
        }
    }

    std::vector<std::filesystem::path> ParseMergeSpec(const toml::value& value) {
        std::vector<std::filesystem::path> ans;
        for (const auto& item : value.as_array()) {
            ans.push_back(toml::get<std::string>(item));
        }
        return ans;
    }
}

TGeneratorSpec ReadGeneratorSpec(const std::filesystem::path& path, ESpecFeatures features) {
    std::ifstream input{path};
    if (!input)
        throw std::system_error{errno, std::system_category(), "failed to open " + path.string()};
    return ReadGeneratorSpec(input, path, features);
}

TGeneratorSpec ReadGeneratorSpec(std::istream& input, const std::filesystem::path& path, ESpecFeatures features) {
    try {
        const auto doc = toml::parse(input, path.string());
        const auto& root = toml::find(doc, NKeys::Root);

        TGeneratorSpec genspec;

        genspec.Root = ParseTargetSpec(root, features);
        for (const auto& [name, tgtspec] : find_or<toml::table>(doc, NKeys::Targets, toml::table{})) {
            genspec.Targets[name] = ParseTargetSpec(tgtspec, features);
        }

        for (const auto& [name, attspec] : find_or<toml::table>(doc, NKeys::Attrs, toml::table{})) {
            genspec.Attrs[name] = ParseAttrsSpec(attspec);
        }

        for (const auto& [name, mergespec] : find_or<toml::table>(doc, NKeys::Merge, toml::table{})) {
            genspec.Merge[name] = ParseMergeSpec(mergespec);
        }

        for (const auto& value : find_or<toml::array>(doc, NKeys::Rules, toml::array{})) {
            ParseRules(value, genspec.Attrs);
        }

        genspec.UseManagedPeersClosure = toml::get<bool>(find_or<toml::value>(doc, NKeys::UseManagedPeersClosure, toml::boolean{false}));

        return genspec;
    } catch (const toml::exception& err) {
        throw TBadGeneratorSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadGeneratorSpec{err.what()};
    }
}
