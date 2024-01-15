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

    static const THashMap<std::string, ECopyLocation> StringToCopyLocation = {
        {"source_root", ECopyLocation::SourceRoot},
        {"generator_root", ECopyLocation::GeneratorRoot},
    };

    THashSet<std::string> CopyLocationSet() {
        THashSet<std::string> result;
        for (const auto& [key, _] : StringToCopyLocation) {
            result.insert(key);
        }
        return result;
    }

    std::string GetCopyLocationList() {
        std::string res = "[";
        bool first = true;
        for (const auto& [key, _] : StringToCopyLocation) {
            if (first) {
                first = false;
            } else {
                res.append(", ");
            }
            res.append(key);
        }
        res.append("]");
        return res;
    }

}

#define VERIFY_GENSPEC(CONDITION, VALUE, ERROR, COMMENT) YEXPORT_VERIFY(CONDITION, TBadGeneratorSpec(), toml::format_error(ERROR, VALUE, COMMENT))

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

    void VerifyFields(const toml::value& value, const THashSet<std::string>& requiredFields, const THashSet<std::string>& optionalFields = {}) {
        if (!value.is_table()) {
            return;
        }
        const auto& table = AsTable(&value);
        for (const auto& fieldName : requiredFields) {
            VERIFY_GENSPEC(table.contains(fieldName), value, NGeneratorSpecError::MissingField, "Should contain field: " + fieldName);
        }
        for (const auto& [key, _] : table) {
            if (!requiredFields.contains(key) && !optionalFields.contains(key)) {
                auto warningMsg = toml::format_error(NGeneratorSpecError::UnknownField, value, "Unknown field [" + key + "] will be ignored: " + key);
                warningMsg = warningMsg.substr(8); // To cut off toml error prefix "[error] "
                spdlog::warn("{}", warningMsg);
            }
        }
    }

    ECopyLocation ParseCopyLocation(const std::string& value, const toml::value& files) {
        auto locationIt = NKeys::StringToCopyLocation.find(value);
        VERIFY_GENSPEC(locationIt != NKeys::StringToCopyLocation.end(), files, NGeneratorSpecError::SpecificationError,
                       "Unknown copy location [" + value + "]. Copy location should be one of the following " + NKeys::GetCopyLocationList());
        return locationIt->second;
    }

    TCopySpec ParseCopySpec(const toml::value& value) {
        VERIFY_GENSPEC(value.is_array() || value.is_table(), value, NGeneratorSpecError::WrongFieldType, "Copy specification should be an array or a table");

        TCopySpec result;
        if (value.is_array()) {
            const auto& arr = AsArray(&value);
            for (const auto& file : arr) {
                result.Items[ECopyLocation::GeneratorRoot].insert(fs::path(AsString(&file)));
            }
        } else if (value.is_table()) {
            for (const auto& [from, files] : AsTable(&value)) {
                auto location = ParseCopyLocation(from, files);
                for (const auto& file : AsArray(&files)) {
                    result.Items[location].insert(fs::path(AsString(&file)));
                }
            }
        }
        return result;
    }

    TTemplate ParseOneTemplate(const toml::value& tmpl) {
        if (tmpl.is_string()) {
            TTemplate templateResult;
            templateResult.Template = AsString(&tmpl);

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
            return templateResult;
        } else if (tmpl.is_table()) {
            VerifyFields(tmpl, {NKeys::Path, NKeys::Dest});
            return {
                    .Template = AsString(&At(&tmpl, NKeys::Path)),
                    .ResultName = AsString(&At(&tmpl, NKeys::Dest)),
                };
        } else {
            throw TBadGeneratorSpec{
                toml::format_error("[error] invalid template value", tmpl,
                                "either path with '.jinja' extension or table "
                                "{path='...', dest='...'} expected")};
        }
    }

    TVector<TTemplate> ParseManyTemplates(const toml::value& tmplArray) {
        TVector<TTemplate> result;
        if (tmplArray.is_array()) {
            for (const auto& tmpl : AsArray(&tmplArray)) {
                result.push_back(ParseOneTemplate(tmpl));
            }
        } else {
            throw TBadGeneratorSpec{
                toml::format_error("[error] invalid templates value", tmplArray,
                                "must be array of templates: either path with '.jinja' extension or table "
                                "{path='...', dest='...'} expected")};
        }
        return result;
    }

    TVector<TTemplate> ParseTemplates(const toml::value& target) {
        bool containsOne = target.contains(NKeys::Template);
        bool containsMany = target.contains(NKeys::ManyTemplates);

        if (!(containsOne ^ containsMany)) {
            throw TBadGeneratorSpec{
                toml::format_error(
                    "[error] invalid attrubutes",
                    target,
                    "must contains template = {path='...', dest='...'} "
                    "or "
                    "templates = [{path='...', dest='...'}, {path='...', dest='...'}]"
                    + std::string((containsOne & containsMany ? " but not together" : ""))
                )
            };
        }

        if (containsMany) {
            return ParseManyTemplates(At(&target, NKeys::ManyTemplates));
        } else {
            return {ParseOneTemplate(At(&target, NKeys::Template))};
        }
    }

    TTargetSpec ParseTargetSpec(const toml::value& target, ESpecFeatures features) {
        VERIFY_GENSPEC(target.is_table(), target, NGeneratorSpecError::WrongFieldType, "Should be a table");
        VerifyFields(target, {}, {NKeys::Template, NKeys::ManyTemplates, NKeys::Copy});

        TTargetSpec targetSpec;
        if (features != CopyFilesOnly) {
            targetSpec.Templates = ParseTemplates(target);
        }
        if (target.contains(NKeys::Copy)) {
            targetSpec.Copy = ParseCopySpec(At(&target, NKeys::Copy));
        }
        return targetSpec;
    }

    std::tuple<std::string, std::optional<TCopySpec>> GetAttrTypeCopy(const toml::value& attrDesc) {
        std::tuple<std::string, std::optional<TCopySpec>> result;
        auto& attrType = std::get<0>(result);
        auto& copySpec = std::get<1>(result);
        if (attrDesc.is_table()) {
            const auto& attrTable = attrDesc.as_table();
            const auto typeIt = attrTable.find(NKeys::Type);
            attrType = typeIt != attrTable.end() ? typeIt->second.as_string() : "";
            if (const auto* valuePtr = Find(&attrDesc, NKeys::Copy)) {
                copySpec = ParseCopySpec(*valuePtr);
            }
        } else {
            attrType = AsString(&attrDesc);
        }
        return result;
    }

    void AddRule(TGeneratorSpec& spec, const TGeneratorRule& rule) {
        if (rule.Useless()) {
            return;
        }
        auto& rules = spec.Rules;
        const auto& ruleId = rules.emplace(rules.size(), rule).first->first;

        for(const auto& attr : rule.Attributes) {
            spec.AttrToRuleId[attr].push_back(ruleId);
        }
    }

    void ParseAttr(TAttrsSpec& attrsSpec, const std::string& attrName, const toml::value& attrDesc, TGeneratorSpec& spec) {
        auto [attrType, copy] = GetAttrTypeCopy(attrDesc);
        EAttrTypes eattrType = EAttrTypes::Unknown;
        std::string curAttrName = attrName;
        std::string_view curAttrType = attrType;
        TFlatAttribute flatAttribute(attrName);
        for (const auto& curAttrName : flatAttribute.BottomUpRange()) {
            if (!attrsSpec.Items.contains(curAttrName)) { // don't overwrite already defined attributes
                if (TryFromString(curAttrType, eattrType)) {
                    attrsSpec.Items[curAttrName].Type = eattrType;
                }
                if (eattrType == EAttrTypes::Unknown) {
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] unknown attr type '" + std::string{curAttrType} + "'", attrDesc, " value must be one of the following data types: " + GetEnumAllNames<EAttrTypes>())
                    );
                }
            } else if (curAttrName.size() != attrName.size()) { // upper attribute
                std::string curAttrNameItem;
                std::string_view mustDictAttrName;
                if (attrsSpec.Items[curAttrName].Type == EAttrTypes::List) { // may be list
                    mustDictAttrName = curAttrNameItem = std::string{curAttrName} + ITEM_TYPE; // then item must be dict
                    if (!attrsSpec.Items.contains(curAttrNameItem)) {
                        attrsSpec.Items[curAttrNameItem].Type = EAttrTypes::Dict;
                    }
                } else{
                    mustDictAttrName = curAttrName; // or parent must be dict
                }
                auto upperAttrType = attrsSpec.Items[mustDictAttrName].Type;
                if (upperAttrType != EAttrTypes::Dict) { // must be dict
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] invalid upper attr " + std::string{mustDictAttrName} + " type '" + ToString<EAttrTypes>(upperAttrType) + "'", attrDesc, " upper attr type must be dict or list")
                    );
                }
            }
            curAttrType = "dict"; // all upper attributes are dicts (if not predefined another)
        }

        if (copy) {
            TGeneratorRule rule;
            rule.Attributes.insert(attrName);
            rule.Copy = *copy;
            AddRule(spec, rule);
        }
    }

    TAttrsSpec ParseAttrsSpec(const toml::value& attrs, TGeneratorSpec& spec) {
        TAttrsSpec attrsSpec;
        for (const auto& item : attrs.as_table()) {
            ParseAttr(attrsSpec, item.first, item.second, spec);
        }
        // By default all list/set/sorted_set items are strings
        for (const auto& [attrName, attrSpec]: attrsSpec.Items) {
            auto type = attrSpec.Type;
            if (type != EAttrTypes::List && type != EAttrTypes::Set && type != EAttrTypes::SortedSet) {
                continue;
            }
            auto attrNameItem = attrName + ITEM_TYPE;
            if (!attrsSpec.Items.contains(attrNameItem)){
                attrsSpec.Items.emplace(attrNameItem, TAttrsSpecValue{.Type = EAttrTypes::Str});
            } else if (type == EAttrTypes::Set || type == EAttrTypes::SortedSet) {
                auto curType = attrsSpec.Items[attrNameItem].Type;
                if (curType != EAttrTypes::Str) {
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] invalid item type " + ToString<EAttrTypes>(curType) + " of " + attrName + " in attrs section", attrs, " now set and sotred_set supports only string items")
                    );
                }
            }
        }
        return attrsSpec;
    }

    TGeneratorRule ParseRule(const toml::value& value) {
        VerifyFields(value, {NKeys::Attrs}, {NKeys::Copy, NKeys::AddValues});

        const auto& attrs = At(&value, NKeys::Attrs);
        const auto& attrArray = AsArray(&attrs);
        VERIFY_GENSPEC(!attrArray.empty(), attrs, NGeneratorSpecError::SpecificationError, "Rule should have one or more attributes");

        TGeneratorRule rule;
        for(const auto& attr : attrArray){
            rule.Attributes.insert(AsString(&attr));
        }
        if (const auto* copy = Find(&value, NKeys::Copy)){
            rule.Copy = ParseCopySpec(*copy);
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

        VERIFY_GENSPEC(!rule.Copy.Useless() || !rule.AddValues.empty(), value, NGeneratorSpecError::SpecificationError, "Module should have non empty field [copy] or [add_values]");
        return rule;
    }

    TVector<fs::path> ParseMergeSpec(const toml::value& value) {
        TVector<fs::path> ans;
        for (const auto& item : AsArray(&value)) {
            ans.push_back(AsString(&item));
        }
        return ans;
    }
}

TGeneratorSpec ReadGeneratorSpec(const fs::path& path, ESpecFeatures features) {
    std::ifstream input{path};
    if (!input)
        throw std::system_error{errno, std::system_category(), "failed to open " + path.string()};
    return ReadGeneratorSpec(input, path, features);
}

TGeneratorSpec ReadGeneratorSpec(std::istream& input, const fs::path& path, ESpecFeatures features) {
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
            attrs[name] = ParseAttrsSpec(attspec, genspec);
        }

        const auto inducedIt = attrs.find(ATTRGROUP_INDUCED);
        if (inducedIt != attrs.end()) {
            auto [targetIt, targetInserted] = attrs.emplace(ATTRGROUP_TARGET, TAttrsSpec{});
            auto& targetItems = targetIt->second.Items;
            for (const auto& [attrName, attrSpec]: inducedIt->second.Items) {
                if (attrName.rfind(ITEM_TYPE) == attrName.size() - ITEM_TYPE.size()) {
                    continue; // skip item descriptions in inducing attributes
                }
                std::string upperAttrName;
                auto firstDivPos = attrName.find(ATTR_DIVIDER);
                const auto& targetAttrName = firstDivPos != std::string::npos ? (upperAttrName = attrName.substr(0, firstDivPos)) : attrName;
                auto targetAttrNameIt = targetItems.find(targetAttrName);
                if (targetAttrNameIt == targetItems.end()) {
                    targetItems[targetAttrName] = TAttrsSpecValue{ .Type = EAttrTypes::List }; // induced attributes are always list in target attributes
                } else {
                    auto& targetAttrSpec = targetAttrNameIt->second;
                    if (targetAttrSpec.Type != EAttrTypes::List) {
                        spdlog::error("non-list induced attribute found {} of type {}, set to list", targetAttrName, ToString<EAttrTypes>(targetAttrSpec.Type));
                        targetAttrSpec.Type = EAttrTypes::List;
                    }
                }

                TAttrsSpecValue upperAttrSpec;
                if (firstDivPos != std::string::npos) {
                    upperAttrSpec = attrSpec;
                    upperAttrSpec.Type = EAttrTypes::Dict;
                }
                const auto& targetAttrItemSpec = firstDivPos != std::string::npos ? upperAttrSpec : attrSpec;
                auto targetAttrItem = targetAttrName + ITEM_TYPE;
                auto targetAttrItemIt = targetItems.find(targetAttrItem);
                if (targetAttrItemIt == targetItems.end()) {
                    targetItems[targetAttrItem] = targetAttrItemSpec; // type of list item
                } else {
                    auto& curTargetAttrItemSpec = targetAttrItemIt->second;
                    if (curTargetAttrItemSpec.Type != targetAttrItemSpec.Type) {
                        spdlog::error("induced attribute item {} type {}, overwritten to {}", targetAttrItem, ToString<EAttrTypes>(curTargetAttrItemSpec.Type), ToString<EAttrTypes>(targetAttrItemSpec.Type));
                        curTargetAttrItemSpec.Type = targetAttrItemSpec.Type;
                    }
                }
            }
        }

        for (const auto& [name, mergespec] : find_or<toml::table>(doc, NKeys::Merge, toml::table{})) {
            genspec.Merge[name] = ParseMergeSpec(mergespec);
        }

        for (const auto& value : find_or<toml::array>(doc, NKeys::Rules, toml::array{})) {
            AddRule(genspec, ParseRule(value));
        }

        genspec.UseManagedPeersClosure = toml::get<bool>(find_or<toml::value>(doc, NKeys::UseManagedPeersClosure, toml::boolean{false}));
        return genspec;
    } catch (const toml::exception& err) {
        throw TBadGeneratorSpec{err.what()};
    } catch (const std::out_of_range& err) {
        throw TBadGeneratorSpec{err.what()};
    }
}



bool TCopySpec::Useless() const {
    for (const auto& [_, files] : Items) {
        if (!files.empty()) {
            return false;
        }
    }
    return true;
}

void TCopySpec::Append(const TCopySpec& copySpec) {
    for (const auto& [location, files] : copySpec.Items) {
        Items[location].insert(files.begin(), files.end());
    }
}

bool TGeneratorRule::Useless() const {
    if (Attributes.empty()) {
        return true;
    }
    if (!Copy.Useless()) {
        return false;
    }
    for (const auto& [_, values] : AddValues) {
        if (!values.empty()) {
            return false;
        }
    }
    return true;
}


TGeneratorSpec::TRuleSet TGeneratorSpec::GetRules(std::string_view attr) const {
    if (auto it = AttrToRuleId.find(attr); it != AttrToRuleId.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

}
