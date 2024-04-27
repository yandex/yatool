#include "generator_spec.h"

#include <contrib/libs/toml11/toml/get.hpp>
#include <contrib/libs/toml11/toml/parser.hpp>
#include <contrib/libs/toml11/toml/value.hpp>

#include <util/generic/set.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <format>

namespace NYexport {

namespace NKeys {
    constexpr const char* Root = "root";
    constexpr const char* Common = "common";// Combine few platforms in one directory
    constexpr const char* Dir = "dir";
    constexpr const char* Template = "template";
    constexpr const char* ManyTemplates = "templates";
    constexpr const char* Path = "path";
    constexpr const char* Dest = "dest";
    constexpr const char* Copy = "copy";
    constexpr const char* Targets = "targets";
    constexpr const char* Platforms = "platforms";
    constexpr const char* Attr = "attr";
    constexpr const char* Attrs = "attrs";
    constexpr const char* Merge = "merge";
    constexpr const char* Type = "type";
    constexpr const char* UseManagedPeersClosure = "use_managed_peers_closure";
    constexpr const char* IgnorePlatforms = "ignore_platforms";
    constexpr const char* Rules = "rules";
    constexpr const char* AddValues = "add_values";
    constexpr const char* Values = "values";

    static const THashMap<std::string, ECopyLocation> StringToCopyLocation = {
        {"source_root", ECopyLocation::SourceRoot},
        {"generator_root", ECopyLocation::GeneratorRoot},
    };

    static const THashMap<std::string, EAttrGroup> StringToAttributeGroup = {
        {"root", EAttrGroup::Root},
        {"target", EAttrGroup::Target},
        {"induced", EAttrGroup::Induced},
        {"dir", EAttrGroup::Directory},
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

    std::string GetAttrGroupList() {
        std::string res = "[";
        bool first = true;
        for (const auto& [key, _] : StringToAttributeGroup) {
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
                result.emplace_back(ParseOneTemplate(tmpl));
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

    jinja2::ValuesMap ParsePlatformsSpec(const toml::value& platforms) {
        jinja2::ValuesMap platformSpec;
        for (const auto& [platform, condition] : AsTable(&platforms)) {
            const auto& conditionStr = AsString(&condition);
            VERIFY_GENSPEC(!conditionStr.empty(), condition, NGeneratorSpecError::SpecificationError, "Should be not empty");
            platformSpec.insert_or_assign(platform, conditionStr);
        }
        return platformSpec;
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

        for(const auto& attr : rule.AttrNames) {
            spec.AttrToRuleIds[attr].emplace_back(ruleId);
        }

        for(const auto& platform : rule.PlatformNames) {
            spec.PlatformToRuleIds[platform].emplace_back(ruleId);
        }
    }

    void ParseAttr(const std::string& attrName, TAttrGroup& attrGroup, const toml::value& attrDesc, TGeneratorSpec& spec) {
        auto [attrType, copy] = GetAttrTypeCopy(attrDesc);
        EAttrTypes eattrType = EAttrTypes::Unknown;
        std::string_view curAttrType = attrType;
        TAttr attribute(attrName);
        for (i32 i = (i32)attribute.Size() - 1; i >= 0; --i) {
            std::string curAttrName(attribute.GetFirstParts(i));
            if (!attrGroup.contains(curAttrName)) { // don't overwrite already defined attributes
                if (TryFromString(curAttrType, eattrType)) {
                    attrGroup[curAttrName] = eattrType;
                }
                if (eattrType == EAttrTypes::Unknown) {
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] unknown attr type '" + std::string{curAttrType} + "'", attrDesc, " value must be one of the following data types: " + GetEnumAllNames<EAttrTypes>())
                    );
                }
            } else if (curAttrName.size() != attrName.size()) { // upper attribute
                std::string mustDictAttrName;
                if (attrGroup[curAttrName] == EAttrTypes::List) { // may be list
                    mustDictAttrName = std::string{curAttrName} + ITEM_SUFFIX; // then item must be dict
                    if (!attrGroup.contains(mustDictAttrName)) {
                        attrGroup[mustDictAttrName] = EAttrTypes::Dict;
                    }
                } else{
                    mustDictAttrName = curAttrName; // or parent must be dict
                }
                auto upperAttrType = attrGroup[mustDictAttrName];
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
            rule.AttrNames.insert(attrName);
            rule.Copy = *copy;
            AddRule(spec, rule);
        }
    }

    TAttrGroup ParseAttrGroup(const toml::value& attrs, TGeneratorSpec& spec) {
        TAttrGroup attrsGroup;
        const auto& attrsTable = attrs.as_table();
        // We must parse attributes from small length to long length
        // for apply parent attribute type before children
        std::vector<const std::string*> attrNames;
        attrNames.reserve(attrsTable.size());
        for (const auto& [attribute, _] : attrsTable) {
            attrNames.emplace_back(&attribute);
        }
        // Sort attributes aphabeticaly for parsing
        Sort(attrNames, [](const std::string* l, const std::string* r) {
            return *l < *r;
        });
        for (auto attrName : attrNames) {
            ParseAttr(*attrName, attrsGroup, attrsTable.at(*attrName), spec);
        }
        // By default all list/set/sorted_set items are strings
        for (const auto& [attrName, attrType]: attrsGroup) {
            if (attrType != EAttrTypes::List && attrType != EAttrTypes::Set && attrType != EAttrTypes::SortedSet) {
                continue;
            }
            auto attrNameItem = attrName.str() + ITEM_SUFFIX;
            if (!attrsGroup.contains(attrNameItem)){
                attrsGroup.emplace(attrNameItem, EAttrTypes::Str);
            } else if (attrType == EAttrTypes::Set || attrType == EAttrTypes::SortedSet) {
                auto curType = attrsGroup[attrNameItem];
                if (curType != EAttrTypes::Str) {
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] invalid item type " + ToString<EAttrTypes>(curType) + " of " + attrName + " in attrs section", attrs, " now set and sotred_set supports only string items")
                    );
                }
            }
        }
        return attrsGroup;
    }

    TGeneratorRule ParseRule(const toml::value& value) {
        VerifyFields(value, {}, {NKeys::Attrs, NKeys::Platforms, NKeys::Copy, NKeys::AddValues});

        const auto* attrs = Find(&value, NKeys::Attrs);
        const auto* platforms = Find(&value, NKeys::Platforms);
        VERIFY_GENSPEC(/* XOR */(bool)attrs != (bool)platforms, value, NGeneratorSpecError::SpecificationError, "Rule should have one of attrs or platforms conditions");

        TGeneratorRule rule;
        if (attrs) {
            const auto& attrArray = AsArray(attrs);
            VERIFY_GENSPEC(!attrArray.empty(), *attrs, NGeneratorSpecError::SpecificationError, "Rule should have one or more attributes");

            for(const auto& attr : attrArray){
                rule.AttrNames.insert(AsString(&attr));
            }
        }
        if (platforms) {
            const auto& platformsArray = AsArray(platforms);
            VERIFY_GENSPEC(!platformsArray.empty(), *platforms, NGeneratorSpecError::SpecificationError, "Rule should have one or more platforms");

            for(const auto& platform : platformsArray){
                rule.PlatformNames.insert(AsString(&platform));
            }
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
            ans.emplace_back(AsString(&item));
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
        const auto& dir = toml::find_or(doc, NKeys::Dir, toml::table{});
        const auto& common = toml::find_or(doc, NKeys::Common, toml::table{});
        if (!dir.empty()) {
            genspec.Dir = ParseTargetSpec(dir, features);
            if (!common.empty()) {
                genspec.Common = ParseTargetSpec(common, features);
            }
        } else if (!common.empty()) {
            spdlog::error("Section {} ignored, because section {} not exists", NKeys::Common, NKeys::Dir);
        }
        for (const auto& [name, tgtspec] : find_or<toml::table>(doc, NKeys::Targets, toml::table{})) {
            genspec.Targets[name] = ParseTargetSpec(tgtspec, features);
        }

        const auto& platforms = find_or<toml::table>(doc, NKeys::Platforms, toml::table{});
        genspec.Platforms = ParsePlatformsSpec(platforms);

        auto& attrGroups = genspec.AttrGroups;
        for (const auto& [name, attrspec] : find_or<toml::table>(doc, NKeys::Attrs, toml::table{})) {
            auto attrGroupIt = NKeys::StringToAttributeGroup.find(name);
            VERIFY_GENSPEC(attrGroupIt != NKeys::StringToAttributeGroup.end(), attrspec, NGeneratorSpecError::SpecificationError,
                    "Unknown attribute group [" + name + "]. Attribute group should be one of the following " + NKeys::GetAttrGroupList());
            attrGroups[attrGroupIt->second] = ParseAttrGroup(attrspec, genspec);
        }

        const auto inducedIt = attrGroups.find(EAttrGroup::Induced);
        if (inducedIt != attrGroups.end()) {
            auto [targetIt, targetInserted] = attrGroups.emplace(EAttrGroup::Target, TAttrGroup{});
            auto& targetItems = targetIt->second;
            for (const auto& [attrName, attrType]: inducedIt->second) {
                if (attrName.IsItem()) {
                    continue; // skip item descriptions in inducing attributes
                }
                const std::string targetAttrName(attrName.GetPart(0));
                auto targetAttrNameIt = targetItems.find(targetAttrName);
                if (targetAttrNameIt == targetItems.end()) {
                    targetItems[targetAttrName] = EAttrTypes::List; // induced attributes are always list in target attributes
                } else {
                    auto& targetAttrType = targetAttrNameIt->second;
                    if (targetAttrType != EAttrTypes::List) {
                        spdlog::error("non-list induced attribute found {} of type {}, set to list", targetAttrName, ToString<EAttrTypes>(targetAttrType));
                        targetAttrType = EAttrTypes::List;
                    }
                }

                EAttrTypes targetAttrItemType = attrName.Size() > 1 ? EAttrTypes::Dict : attrType;
                auto targetAttrItem = targetAttrName + ITEM_SUFFIX;
                auto targetAttrItemIt = targetItems.find(targetAttrItem);
                if (targetAttrItemIt == targetItems.end()) {
                    targetItems[targetAttrItem] = targetAttrItemType; // type of list item
                } else {
                    auto& curTargetAttrItemType = targetAttrItemIt->second;
                    if (curTargetAttrItemType != targetAttrItemType) {
                        spdlog::error("induced attribute item {} type {}, overwritten to {}", targetAttrItem, ToString<EAttrTypes>(curTargetAttrItemType), ToString<EAttrTypes>(targetAttrItemType));
                        curTargetAttrItemType = targetAttrItemType;
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
        genspec.IgnorePlatforms = toml::get<bool>(find_or<toml::value>(doc, NKeys::IgnorePlatforms, toml::boolean{false}));
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
    if (AttrNames.empty() && PlatformNames.empty()) {
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

TGeneratorSpec::TRuleSet TGeneratorSpec::GetAttrRules(const std::string_view attr) const {
    if (auto it = AttrToRuleIds.find(attr); it != AttrToRuleIds.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

TGeneratorSpec::TRuleSet TGeneratorSpec::GetPlatformRules(const std::string_view platform) const {
    if (auto it = PlatformToRuleIds.find(platform); it != PlatformToRuleIds.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

}
