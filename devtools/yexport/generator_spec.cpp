#include "generator_spec.h"

#include <contrib/libs/toml11/include/toml11/types.hpp>
#include <contrib/libs/toml11/include/toml11/get.hpp>
#include <contrib/libs/toml11/include/toml11/parser.hpp>
#include <contrib/libs/toml11/include/toml11/find.hpp>

#include <util/generic/set.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <fstream>
#include <format>

namespace NYexport {

namespace NKeys {
    constexpr const char* Root = "root";
    constexpr const char* Template = "template";
    constexpr const char* Templates = "templates";
    constexpr const char* MergePlatformTemplate = "merge_platform_template";
    constexpr const char* MergePlatformTemplates = "merge_platform_templates";
    constexpr const char* Path = "path";
    constexpr const char* Dest = "dest";
    constexpr const char* Copy = "copy";
    constexpr const char* IsExtraTarget = "is_extra_target";
    constexpr const char* IsTest = "is_test";
    constexpr const char* Targets = "targets";
    constexpr const char* Platforms = "platforms";
    constexpr const char* Attr = "attr";
    constexpr const char* Attrs = "attrs";
    constexpr const char* Merge = "merge";
    constexpr const char* UseManagedPeersClosure = "use_managed_peers_closure";
    constexpr const char* IgnorePlatforms = "ignore_platforms";
    constexpr const char* SourceRootReplacer = "source_root_replacer";
    constexpr const char* BinaryRootReplacer = "binary_root_replacer";
    constexpr const char* Rules = "rules";
    constexpr const char* AddValues = "add_values";
    constexpr const char* Values = "values";

    static const THashMap<std::string, ECopyLocation> StringToCopyLocation = {
        {"source_root", ECopyLocation::SourceRoot},
        {"generator_root", ECopyLocation::GeneratorRoot},
    };

    static const THashMap<std::string, EAttrGroup> StringToAttributeGroup = {
        {"root", EAttrGroup::Root},
        {"platform", EAttrGroup::Platform},
        {"dir", EAttrGroup::Directory},
        {"target", EAttrGroup::Target},
        {"induced", EAttrGroup::Induced},
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

#define VERIFY_GENSPEC(CONDITION, VALUE, ERROR, COMMENT) YEXPORT_VERIFY(CONDITION, TBadGeneratorSpec(), "[error] " + toml::format_error(ERROR, VALUE, COMMENT))

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
        return value->as_string();
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

    TTemplateSpec ParseOneTemplate(const toml::value& tmpl) {
        if (tmpl.is_string()) {
            TTemplateSpec templateResult;
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

    TVector<TTemplateSpec> ParseManyTemplates(const toml::value& tmplArray) {
        TVector<TTemplateSpec> result;
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

    TVector<TTemplateSpec> ParseSomeTemplates(const toml::value& target, const char* keyTemplate, const char* keyTemplates, bool mayBeEmpty = false) {
        bool containsOne = target.contains(keyTemplate);
        bool containsMany = target.contains(keyTemplates);
        if (mayBeEmpty && !containsOne && !containsMany) {
            return {};
        }
        if (!(containsOne ^ containsMany)) {
            throw TBadGeneratorSpec{
                toml::format_error(
                    "[error] invalid attributes",
                    target,
                    "must contains template = {path='...', dest='...'} "
                    "or "
                    "templates = [{path='...', dest='...'}, {path='...', dest='...'}]"
                    + std::string((containsOne & containsMany ? ", but not together" : ""))
                )
            };
        }
        if (containsMany) {
            return ParseManyTemplates(At(&target, keyTemplates));
        } else {
            return {ParseOneTemplate(At(&target, keyTemplate))};
        }
    }

    TVector<TTemplateSpec> ParseTemplates(const toml::value& target) {
        return ParseSomeTemplates(target, NKeys::Template, NKeys::Templates);
    }

    TVector<TTemplateSpec> ParseMergePlatformTemplates(const toml::value& target) {
        return ParseSomeTemplates(target, NKeys::MergePlatformTemplate, NKeys::MergePlatformTemplates, true);
    }

    TTargetSpec ParseTargetSpec(const toml::value& target, bool ignorePlatforms, bool mayBeExtraTarget = false) {
        VERIFY_GENSPEC(target.is_table(), target, NGeneratorSpecError::WrongFieldType, "Should be a table");

        TTargetSpec targetSpec;
        if (mayBeExtraTarget && target.contains(NKeys::IsExtraTarget)) {
            targetSpec.IsExtraTarget = toml::find<bool>(target, NKeys::IsExtraTarget);
        };
        if (targetSpec.IsExtraTarget) {
            VerifyFields(target, {}, {NKeys::Copy, NKeys::IsExtraTarget, NKeys::IsTest});
            if (target.contains(NKeys::IsTest)) {
                targetSpec.IsTest = toml::find<bool>(target, NKeys::IsTest);
            }
        } else if (ignorePlatforms) {
            VerifyFields(target, {}, {NKeys::Template, NKeys::Templates, NKeys::Copy});
        } else {
            VerifyFields(target, {}, {NKeys::Template, NKeys::Templates, NKeys::MergePlatformTemplate, NKeys::MergePlatformTemplates, NKeys::Copy});
        }

        if (!targetSpec.IsExtraTarget) {
            targetSpec.Templates = ParseTemplates(target);
            if (!ignorePlatforms) {
                targetSpec.MergePlatformTemplates = ParseMergePlatformTemplates(target);
                if (!targetSpec.MergePlatformTemplates.empty() && targetSpec.Templates.size() != targetSpec.MergePlatformTemplates.size()) {
                    throw TBadGeneratorSpec{
                        toml::format_error(
                            "[error] invalid attributes",
                            target,
                            "must contains equal count of template(s) and merge_platform_template(s)"
                        )
                    };
                }
            }
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

    void AddRule(TGeneratorSpec& spec, const TGeneratorRule& rule) {
        if (rule.Useless()) {
            return;
        }
        auto& rules = spec.Rules;
        const auto& ruleId = rules.emplace(rules.size(), rule).first->first;

        for(const auto& attrName : rule.AttrNames) {
            spec.AttrToRuleIds[attrName].emplace_back(ruleId);
        }

        for(const auto& attrWithValue : rule.AttrWithValues) {
            spec.AttrWithValuesToRuleIds[attrWithValue].emplace_back(ruleId);
        }

        for(const auto& platformName : rule.PlatformNames) {
            spec.PlatformToRuleIds[platformName].emplace_back(ruleId);
        }
    }

    void ParseAttr(const std::string& attrName, TAttrGroup& attrGroup, const toml::value& attrDesc) {
        auto attrType = AsString(&attrDesc);
        EAttrTypes eattrType = EAttrTypes::Unknown;
        std::string_view curAttrType = attrType;
        TAttr attr(attrName);
        for (i32 i = (i32)attr.Size() - 1; i >= 0; --i) {
            std::string curAttrName(attr.GetFirstParts(i));
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
    }

    TAttrGroup ParseAttrGroup(const toml::value& attrs) {
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
            ParseAttr(*attrName, attrsGroup, attrsTable.at(*attrName));
        }
        // By default all list/set/sorted_set items are strings
        TAttrGroup resAttrsGroup;
        for (const auto& [attrName, attrType]: attrsGroup) {
            resAttrsGroup.emplace(attrName, attrType);
            if (attrType != EAttrTypes::List && attrType != EAttrTypes::Set && attrType != EAttrTypes::SortedSet) {
                continue;
            }
            auto attrNameItem = attrName.str() + ITEM_SUFFIX;
            if (!attrsGroup.contains(attrNameItem)){
                resAttrsGroup.emplace(attrNameItem, EAttrTypes::Str);
            } else if (attrType == EAttrTypes::Set || attrType == EAttrTypes::SortedSet) {
                auto curType = attrsGroup[attrNameItem];
                if (curType != EAttrTypes::Str) {
                    throw TBadGeneratorSpec(
                        toml::format_error("[error] invalid item type " + ToString<EAttrTypes>(curType) + " of " + attrName + " in attrs section", attrs, " now set and sorted_set supports only string items")
                    );
                }
            }
        }
        return resAttrsGroup;
    }

    TGeneratorRule ParseRule(const toml::value& value, bool ignorePlatforms) {
        VerifyFields(value, {}, {NKeys::Attrs, NKeys::Platforms, NKeys::Copy, NKeys::AddValues});

        const auto* attrs = Find(&value, NKeys::Attrs);
        const auto* platforms = Find(&value, NKeys::Platforms);
        if (ignorePlatforms && platforms) {
            throw TBadGeneratorSpec(
                toml::format_error("Rule for platforms can't be defined with enabled " + std::string{NKeys::IgnorePlatforms}, *platforms, " remove rule for platforms or disable " + std::string{NKeys::IgnorePlatforms})
            );
        }
        VERIFY_GENSPEC(/* XOR */(bool)attrs != (bool)platforms, value, NGeneratorSpecError::SpecificationError, "Rule should have one of attrs or platforms conditions");

        TGeneratorRule rule;
        if (attrs) {
            const auto& attrArray = AsArray(attrs);
            VERIFY_GENSPEC(!attrArray.empty(), *attrs, NGeneratorSpecError::SpecificationError, "Rule should have one or more attributes");

            for(const auto& attr : attrArray){
                auto attrItem = AsString(&attr);
                if (attrItem.find('=') == std::string::npos) {
                    rule.AttrNames.insert(attrItem);
                } else {
                    rule.AttrWithValues.insert(attrItem);
                }
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

TGeneratorSpec ReadGeneratorSpec(const fs::path& path) {
    std::ifstream input{path};
    if (!input)
        throw std::system_error{errno, std::system_category(), "failed to open " + path.string()};
    return ReadGeneratorSpec(input, path);
}

TGeneratorSpec ReadGeneratorSpec(std::istream& input, const fs::path& path) {
    try {
        const auto doc = toml::parse(input, path.string());
        const auto& root = toml::find(doc, NKeys::Root);

        TGeneratorSpec genspec;

        if (doc.contains(NKeys::UseManagedPeersClosure)) {
            genspec.UseManagedPeersClosure = find<bool>(doc, NKeys::UseManagedPeersClosure);
        }
        if (doc.contains(NKeys::IgnorePlatforms)) {
            genspec.IgnorePlatforms = find<bool>(doc, NKeys::IgnorePlatforms);
        }
        if (doc.contains(NKeys::SourceRootReplacer)) {
            genspec.SourceRootReplacer = find<std::string>(doc, NKeys::SourceRootReplacer);
        }
        if (doc.contains(NKeys::BinaryRootReplacer)) {
            genspec.BinaryRootReplacer = find<std::string>(doc, NKeys::BinaryRootReplacer);
        }

        genspec.Root = ParseTargetSpec(root, genspec.IgnorePlatforms);
        if (doc.contains(NKeys::Targets)) {
            for (const auto& [name, tgtspec] : find<toml::table>(doc, NKeys::Targets)) {
                genspec.Targets[name] = ParseTargetSpec(tgtspec, genspec.IgnorePlatforms, true);
            }
        }

        if (genspec.IgnorePlatforms) {
            if (doc.contains(NKeys::Platforms)) {
                throw TBadGeneratorSpec(
                    toml::format_error("If enabled " + std::string{NKeys::IgnorePlatforms} + ", can't be defined " + NKeys::Platforms, doc, " disable " + std::string{NKeys::IgnorePlatforms} + " or remove " + std::string{NKeys::Platforms})
                );
            }
        } else {
            if (!doc.contains(NKeys::Platforms)) {
                throw TBadGeneratorSpec(
                    toml::format_error("Section " + std::string{NKeys::Platforms} + " required when disabled " + std::string{NKeys::IgnorePlatforms}, doc, " add " + std::string{NKeys::Platforms} + " or enable " + std::string{NKeys::IgnorePlatforms})
                );
            }
            const auto& platforms = find<toml::table>(doc, NKeys::Platforms);
            genspec.Platforms = ParsePlatformsSpec(platforms);
        }

        auto& attrGroups = genspec.AttrGroups;
        if (doc.contains(NKeys::Attrs)) {
            for (const auto& [name, attrspec] : find<toml::table>(doc, NKeys::Attrs)) {
                auto attrGroupIt = NKeys::StringToAttributeGroup.find(name);
                VERIFY_GENSPEC(attrGroupIt != NKeys::StringToAttributeGroup.end(), attrspec, NGeneratorSpecError::SpecificationError,
                        "Unknown attribute group [" + name + "]. Attribute group should be one of the following " + NKeys::GetAttrGroupList());
                if (attrGroupIt->second == EAttrGroup::Platform && genspec.IgnorePlatforms) {
                    throw TBadGeneratorSpec(
                        toml::format_error("If enabled " + std::string{NKeys::IgnorePlatforms} + ", can't be defined attrs for " + attrGroupIt->first, doc, " disable " + std::string{NKeys::IgnorePlatforms} + " or attrs for " + attrGroupIt->first)
                    );
                }
                attrGroups[attrGroupIt->second] = ParseAttrGroup(attrspec);
            }
        }

        const auto inducedIt = attrGroups.find(EAttrGroup::Induced);
        if (inducedIt != attrGroups.end()) {
            auto [targetIt, targetInserted] = attrGroups.emplace(EAttrGroup::Target, TAttrGroup{});
            auto& targetItems = targetIt->second;
            for (const auto& [attrName, attrType]: inducedIt->second) {
                if (attrName.IsItem()) {
                    continue; // skip item descriptions in inducing attributes
                }
                const std::string targetAttrName(attrName.GetPart(0));// induce always first part of attribute
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

                auto inducedAttrItemIt = inducedIt->second.find(std::string{attrName.GetPart(0)} + ITEM_SUFFIX);
                // If first part of induced has defined ITEM type, use it, else use Dict (if parts > 1) or attrType (if parts == 1)
                EAttrTypes targetAttrItemType = inducedAttrItemIt != inducedIt->second.end() ? inducedAttrItemIt->second : (attrName.Size() > 1 ? EAttrTypes::Dict : attrType);
                auto targetAttrItem = targetAttrName + ITEM_SUFFIX;
                auto targetAttrItemIt = targetItems.find(targetAttrItem);
                if (targetAttrItemIt == targetItems.end()) {
                    targetItems[targetAttrItem] = targetAttrItemType; // type of list item
                } else {
                    auto& curTargetAttrItemType = targetAttrItemIt->second;
                    if (curTargetAttrItemType != targetAttrItemType) {
                        spdlog::error("induced attribute item {} type {}, overwritten to {} [ by inducing {} ]", targetAttrItem, ToString<EAttrTypes>(curTargetAttrItemType), ToString<EAttrTypes>(targetAttrItemType), attrName.str());
                        curTargetAttrItemType = targetAttrItemType;
                    }
                }
            }
        }

        if (doc.contains(NKeys::Merge)) {
            for (const auto& [name, mergespec] : find<toml::table>(doc, NKeys::Merge)) {
                genspec.Merge[name] = ParseMergeSpec(mergespec);
            }
        }

        if (doc.contains(NKeys::Rules)) {
            for (const auto& value : find<toml::array>(doc, NKeys::Rules)) {
                AddRule(genspec, ParseRule(value, genspec.IgnorePlatforms));
            }
        }

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
    if (AttrNames.empty() && AttrWithValues.empty() && PlatformNames.empty()) {
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

TGeneratorSpec::TRuleSet TGeneratorSpec::GetAttrRules(const std::string& attrName) const {
    if (auto it = AttrToRuleIds.find(attrName); it != AttrToRuleIds.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

TGeneratorSpec::TRuleSet TGeneratorSpec::GetAttrWithValueRules(const std::string& attrName, const std::span<const std::string>& attrValue) const {
    std::string attrWithValue = attrName;
    std::string eq = "=";
    std::string space = " ";
    std::string& glue = eq;
    for (const auto& attrValueItem: attrValue) {
        attrWithValue += glue;
        attrWithValue += attrValueItem;
        glue = space;
    }
    if (auto it = AttrWithValuesToRuleIds.find(attrWithValue); it != AttrWithValuesToRuleIds.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

TGeneratorSpec::TRuleSet TGeneratorSpec::GetPlatformRules(const std::string& platformName) const {
    if (auto it = PlatformToRuleIds.find(platformName); it != PlatformToRuleIds.end()) {
        TRuleSet result;
        for (const auto& id : it->second) {
            result.insert(&Rules.at(id));
        }
        return result;
    }
    return {};
}

}
