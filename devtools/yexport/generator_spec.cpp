#include "generator_spec.h"

#include <contrib/libs/toml11/toml/get.hpp>
#include <contrib/libs/toml11/toml/parser.hpp>
#include <contrib/libs/toml11/toml/value.hpp>

#include <util/generic/set.h>

#include <spdlog/spdlog.h>

#include <fstream>

namespace NYexport {


namespace NKeys {
    constexpr const char* Root = "root";
    constexpr const char* Template = "template";
    constexpr const char* ManyTemplates = "templates";
    constexpr const char* Path = "path";
    constexpr const char* Dest = "dest";
    constexpr const char* Copy = "copy";
    constexpr const char* Targets = "targets";
    constexpr const char* Attr = "attr";
    constexpr const char* Attrs = "attrs";
    constexpr const char* Merge = "merge";
    constexpr const char* Type = "type";
    constexpr const char* UseManagedPeersClosure = "use_managed_peers_closure";
    constexpr const char* Rules = "rules";
    constexpr const char* AddValues = "add_values";
    constexpr const char* Values = "values";
}

#define VERIFY_GENSPEC(CONDITION, VALUE, ERROR, COMMENT) YEXPORT_VERIFY(CONDITION, TBadGeneratorSpec(), toml::format_error(ERROR, VALUE, COMMENT))


TGeneratorSpec::TRuleSet TGeneratorSpec::GetRules(const std::string attr) const {
    if (auto it = AttrToRuleId.find(attr); it != AttrToRuleId.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

namespace {
    //Following functions take pointer to a value in order to prevent implicit construction of new value from vector/unordered_map and following segfault
    const auto& AsTable(const toml::value* value) {
        VERIFY_GENSPEC(value->is_table(), *value, NGeneratorSpecError::WrongFieldType, "Should be a table");
        return value->as_table();
    }
    const auto& AsArray(const toml::value* value) {
        VERIFY_GENSPEC(value->is_array(), *value, NGeneratorSpecError::WrongFieldType, "Should be an array");
        return value->as_array();
    }
    const auto& AsString(const toml::value* value) {
        VERIFY_GENSPEC(value->is_string(), *value, NGeneratorSpecError::WrongFieldType, "Should be a string");
        return value->as_string().str;
    }
    const auto& At(const toml::value* value, const std::string& fieldName) {
        const auto& table = AsTable(value);
        VERIFY_GENSPEC(table.contains(fieldName), *value, NGeneratorSpecError::MissingField, "Should contain field: " + fieldName);
        return table.at(fieldName);
    }
    const toml::value* Find(const toml::value* value, const std::string& fieldName) {
        const auto& table = AsTable(value);
        auto it = table.find(fieldName);
        if (it != table.end()) {
            return &(it->second);
        }
        return nullptr;
    }
    THashSet<fs::path> ExtractPathes(const toml::value* value) {
        THashSet<fs::path> result;
        const auto& arr = AsArray(value);
        for (const auto& file : arr) {
            result.insert(fs::path(AsString(&file)));
        }
        return result;
    }

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
        const auto* copy = Find(&target, NKeys::Copy);
        if (copy) {
            const auto files = ExtractPathes(copy);
            targetSpec.Copy.insert(files.begin(), files.end());
        }
        return targetSpec;
    }

    std::tuple<std::string, const toml::array*> GetAttrTypeCopy(const toml::value& attrDesc) {
        std::string attrType;
        const toml::array* copy;
        if (attrDesc.is_table()) {
            const auto& attrTable = attrDesc.as_table();
            const auto typeIt = attrTable.find(NKeys::Type);
            attrType = typeIt != attrTable.end() ? typeIt->second.as_string() : "";
            const auto copyIt = attrTable.find(NKeys::Copy);
            copy = copyIt != attrTable.end() ? &copyIt->second.as_array() : nullptr;
        } else {
            attrType = attrDesc.as_string();
            copy = nullptr;
        }
        return { attrType, copy };
    }

    void ParseAttr(TAttrsSpec& attrsSpec, const std::string& attrName, const toml::value& attrDesc) {
        auto [attrType, copy] = GetAttrTypeCopy(attrDesc);
        EAttrTypes eattrType = EAttrTypes::Unknown;
        std::string curAttrName = attrName;
        std::string_view curAttrType = attrType;
        do {
            if (!attrsSpec.Items.contains(curAttrName)) { // don't overwrite already defined attributes
                if (TryFromString(curAttrType, eattrType)) {
                    attrsSpec.Items[curAttrName].Type = eattrType;
                }
                if (eattrType == EAttrTypes::Unknown) {
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] invalid attr type", attrDesc, " value must be one of the following data types: " + GetEnumAllNames<EAttrTypes>())
                    );
                }
            } else if (curAttrName.size() != attrName.size() // upper attribute
                        && attrsSpec.Items[curAttrName].Type != EAttrTypes::Dict) { // must be dict
                throw TBadGeneratorSpec(
                    toml::format_error("[error] invalid upper attr " + curAttrName + " type", attrDesc, " upper attr type must be dict")
                );
            }
            if (copy) { // append copy values to current attribute spec
                auto& attrCopy = attrsSpec.Items[curAttrName].Copy;
                for (const auto& copyItem : *copy) {
                    if (copyItem.is_string()) {
                        attrCopy.insert(fs::path{std::string(copyItem.as_string())});
                    } else {
                        throw TBadGeneratorSpec(
                            toml::format_error("[error] invalid copy item value", copyItem, " each copy item must be string")
                        );
                    }
                }
            }
            // Search attribute divider in attribute name
            if (auto divPos = curAttrName.rfind(ATTR_DIVIDER); divPos == std::string::npos) {
                break;
            } else {
                curAttrName = curAttrName.substr(0, divPos);
                curAttrType = "dict"; // all upper attributes are dicts (if not predefined another)
            }
        } while (true);
    }

    TAttrsSpec ParseAttrsSpec(const toml::value& attrs) {
        TAttrsSpec attrsSpec;
        for (const auto& item : attrs.as_table()) {
            ParseAttr(attrsSpec, item.first, item.second);
        }
        return attrsSpec;
    }

    TGeneratorRule ParseRule(const toml::value& value) {
        const auto& attrs = At(&value, NKeys::Attrs);
        const auto& attrArray = AsArray(&attrs);
        VERIFY_GENSPEC(!attrArray.empty(), value, NGeneratorSpecError::SpecificationError, "Rule should have one or more attributes");

        TGeneratorRule rule;
        for(const auto& attr : attrArray){
            rule.Attributes.insert(AsString(&attr));
        }
        if (const auto* copy = Find(&value, NKeys::Copy); copy){
            rule.Copy = ExtractPathes(copy);
        }
        if (const auto* add_values= Find(&value, NKeys::AddValues); add_values){
            for(const auto& add_value : AsArray(add_values)) {
                const auto& attr = AsString(&At(&add_value, NKeys::Attr));
                const auto& values = At(&add_value, NKeys::Values);
                const auto& valuesArray = AsArray(&values);
                VERIFY_GENSPEC(!valuesArray.empty(), values, NGeneratorSpecError::SpecificationError, "add_value should have one or more values to add");
                for (const auto& value : valuesArray) {
                    rule.AddValues[attr].push_back(AsString(&value));
                }
            }
        }

        VERIFY_GENSPEC(!rule.Copy.empty() || !rule.AddValues.empty(), value, NGeneratorSpecError::SpecificationError, "Module should have non empty field [copy] or [add_values]");
        return rule;
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

        auto& attrs = genspec.Attrs;
        for (const auto& [name, attspec] : find_or<toml::table>(doc, NKeys::Attrs, toml::table{})) {
            attrs[name] = ParseAttrsSpec(attspec);
        }

        const auto inducedIt = attrs.find(ATTRGROUP_INDUCED);
        if (inducedIt != attrs.end()) {
            auto [targetIt, targetInserted] = attrs.emplace(ATTRGROUP_TARGET, TAttrsSpec{});
            auto& targetItems = targetIt->second.Items;
            for (const auto& [attrName, attrSpec]: inducedIt->second.Items) {
                std::string upperAttrName;
                auto lastDivPos = attrName.find(ATTR_DIVIDER);
                const auto& targetAttrName = lastDivPos != std::string::npos ? (upperAttrName = attrName.substr(0, lastDivPos)) : attrName;
                auto targetAttrNameIt = targetItems.find(targetAttrName);
                if (targetAttrNameIt == targetItems.end()) {
                    targetItems[targetAttrName] = TAttrsSpecValue{ .Type = EAttrTypes::List }; // induced attributes are always list in target attributes
                } else {
                    auto& targetAttrSpec = targetAttrNameIt->second;
                    if (targetAttrSpec.Type != EAttrTypes::List) {
                        spdlog::error("non-list induced attribute found {} of type {}, set to list", targetAttrName, (int)targetAttrSpec.Type);
                        targetAttrSpec.Type = EAttrTypes::List;
                    }
                }

                TAttrsSpecValue upperAttrSpec;
                if (lastDivPos != std::string::npos) {
                    upperAttrSpec = attrSpec;
                    upperAttrSpec.Type = EAttrTypes::Dict;
                }
                const auto& targetAttrItemSpec = lastDivPos != std::string::npos ? upperAttrSpec : attrSpec;
                auto targetAttrItem = targetAttrName + LIST_ITEM_TYPE;
                auto targetAttrItemIt = targetItems.find(targetAttrItem);
                if (targetAttrItemIt == targetItems.end()) {
                    targetItems[targetAttrItem] = targetAttrItemSpec; // type of list item
                } else {
                    auto& curTargetAttrItemSpec = targetAttrItemIt->second;
                    if (curTargetAttrItemSpec.Type != targetAttrItemSpec.Type) {
                        spdlog::error("induced attribute item {} type {}, overwritten to {}", targetAttrItem, (int)curTargetAttrItemSpec.Type, (int)targetAttrItemSpec.Type);
                        curTargetAttrItemSpec.Type = targetAttrItemSpec.Type;
                    }
                    if (curTargetAttrItemSpec.Copy.empty()) {
                        curTargetAttrItemSpec.Copy = targetAttrItemSpec.Copy;
                    } else if (!targetAttrItemSpec.Copy.empty()) { // merge sets
                        for (const auto& copyItem : targetAttrItemSpec.Copy) {
                            curTargetAttrItemSpec.Copy.emplace(copyItem);
                        }
                    }
                }
            }
        }

        for (const auto& [name, mergespec] : find_or<toml::table>(doc, NKeys::Merge, toml::table{})) {
            genspec.Merge[name] = ParseMergeSpec(mergespec);
        }

        for (const auto& value : find_or<toml::array>(doc, NKeys::Rules, toml::array{})) {
            auto& rules = genspec.Rules;
            const auto& [ruleId, rule] = *rules.emplace(rules.size(), ParseRule(value)).first;

            for(const auto& attr : rule.Attributes) {
                genspec.AttrToRuleId[attr].push_back(ruleId);
            }
        }

        genspec.UseManagedPeersClosure = toml::get<bool>(find_or<toml::value>(doc, NKeys::UseManagedPeersClosure, toml::boolean{false}));
        return genspec;
    } catch (const toml::exception& err) {
        throw TBadGeneratorSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadGeneratorSpec{err.what()};
    }
}

}
