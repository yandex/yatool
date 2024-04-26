#include "attributes.h"

#include <spdlog/spdlog.h>

#include <util/generic/set.h>
#include <util/string/type.h>

namespace NYexport {

TAttrsPtr TAttrs::Create(const TAttrGroup& attrGroup, const std::string& name) {
    return MakeSimpleShared<TAttrs>(attrGroup, name);
}

TAttrs::TAttrs(const TAttrGroup& attrGroup, const std::string& name)
    : AttrGroup_(attrGroup)
    , Name_(name)
{}

void TAttrs::SetAttrValue(jinja2::ValuesMap& attrs, const TAttr& attr, const jinja2::ValuesList& values, size_t atPos, TGetDebugStr getDebugStr) const {
    if (atPos == (attr.Size() - 1)) {// last item - append complex attribute or set simple attribute
        auto attrType = GetAttrType(attr.GetFirstParts(atPos));
        auto attrName = attr.GetPart(atPos);
        switch (attrType) {
            case EAttrTypes::Str:
                SetStrAttr(attrs, attrName, GetSimpleAttrValue(attrType, values, getDebugStr), getDebugStr);
                break;
            case EAttrTypes::Bool:
                SetBoolAttr(attrs, attrName, GetSimpleAttrValue(attrType, values, getDebugStr), getDebugStr);
                break;
            case EAttrTypes::Flag:
                SetFlagAttr(attrs, attrName, GetSimpleAttrValue(attrType, values, getDebugStr), getDebugStr);
                break;
            case EAttrTypes::List:{
                auto [listAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
                AppendToListAttr(listAttrIt->second.asList(), attr, values, getDebugStr);
            }; break;
            case EAttrTypes::Set:{
                auto [setAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
                AppendToSetAttr(setAttrIt->second.asList(), attr, values, getDebugStr);
            }; break;
            case EAttrTypes::SortedSet:{
                auto [sortedSetAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
                AppendToSortedSetAttr(sortedSetAttrIt->second.asList(), attr, values, getDebugStr);
            }; break;
            case EAttrTypes::Dict:{
                auto [dictAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesMap{});
                AppendToDictAttr(dictAttrIt->second.asMap(), attr, values, getDebugStr);
            }; break;
            default:
                spdlog::error("Skipped unknown {}", getDebugStr());
        }
    } else { // middle item - create list/dict attribute
        auto attrType = GetAttrType(attr.GetFirstParts(atPos));
        auto attrName = attr.GetPart(atPos);
        switch (attrType) {
            case EAttrTypes::List:{// only special case may be here - list of dicts
                auto listItem = std::string{attr.GetFirstParts(atPos)} + ITEM_SUFFIX;
                auto itemAttrType = GetAttrType(std::string{listItem});
                if (itemAttrType != EAttrTypes::Dict) {
                    spdlog::error("trying create middle item {} which is list of {} at {}", attrName, ToString<EAttrTypes>(itemAttrType), getDebugStr());
                    break;
                }
                auto [listAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
                auto& list = listAttrIt->second.asList();
                if (listItem == attr.str()) {// magic attribute <list>-ITEM must append new empty item
                    list.emplace_back(jinja2::ValuesMap{});
                    break;
                }
                if (list.empty()) {
                    spdlog::error("trying set item of empty list {} at {}", attrName, getDebugStr());
                    break;
                }
                // Always apply attributes to last item of list
                SetAttrValue(list.back().asMap(), attr, values, atPos + 1, getDebugStr);
            }; break;
            case EAttrTypes::Dict:{
                auto [dictAttrIt, _] = attrs.emplace(attrName, jinja2::ValuesMap{});
                SetAttrValue(dictAttrIt->second.asMap(), attr, values, atPos + 1, getDebugStr);
            }; break;
            default:
                spdlog::error("Can't create middle {} for type {} of {}", attrName, ToString<EAttrTypes>(attrType), getDebugStr());
        }
    }
}

void TAttrs::SetStrAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const jinja2::Value& value, TGetDebugStr getDebugStr) const {
    const auto& v = value.asString();
    const auto [attrIt, inserted] = attrs.emplace(attrName, value);
    if (!inserted && attrIt->second.asString() != v) {
        spdlog::error("Set string value '{}' of {}, but it already has value '{}', overwritten", v, getDebugStr(), attrIt->second.asString());
        attrIt->second = value;
    }
}

void TAttrs::SetBoolAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const jinja2::Value& value, TGetDebugStr getDebugStr) const {
    const auto v = value.get<bool>();
    const auto [attrIt, inserted] = attrs.emplace(attrName, value);
    if (!inserted && attrIt->second.get<bool>() != v) {
        spdlog::error("Set bool value {} of {}, but it already has value {}, overwritten", v ? "True" : "False", getDebugStr(), attrIt->second.get<bool>() ? "True" : "False");
        attrIt->second = value;
    }
}

void TAttrs::SetFlagAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const jinja2::Value& value, TGetDebugStr /*getDebugStr*/) const {
    attrs.insert_or_assign(std::string{attrName}, value);
}

void TAttrs::AppendToListAttr(jinja2::ValuesList& listAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    auto itemAttrType = GetItemAttrType(attr.str());
    for (const auto& value : values) {
        auto item = GetSimpleAttrValue(itemAttrType, jinja2::ValuesList{value}, getDebugStr);
        if (item.isEmpty()) {
            continue;
        }
        listAttr.emplace_back(std::move(item));
    }
}

void TAttrs::AppendToSetAttr(jinja2::ValuesList& setAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    auto itemAttrType = GetItemAttrType(attr.str());
    for (const auto& value : values) {
        auto item = GetSimpleAttrValue(itemAttrType, jinja2::ValuesList{value}, getDebugStr);
        if (item.isEmpty()) {
            continue;
        }
        bool exists = false;
        for (const auto& v : setAttr) {
            if (exists |= v.asString() == item.asString()) {
                break;
            }
        }
        if (!exists) { // add to list only if not exists
            setAttr.emplace_back(std::move(item));
        }
    }
}

void TAttrs::AppendToSortedSetAttr(jinja2::ValuesList& sortedSetAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    auto itemAttrType = GetItemAttrType(attr.str());
    std::set<std::string> set;
    if (!sortedSetAttr.empty()) {
        for (const auto& item : sortedSetAttr) { // fill set by exists values
            set.emplace(item.asString());
        }
    }
    for (const auto& value : values) {
        auto item = GetSimpleAttrValue(itemAttrType, jinja2::ValuesList{value}, getDebugStr);
        if (item.isEmpty()) {
            continue;
        }
        set.emplace(item.asString());// append new values
    }
    sortedSetAttr.clear();
    for (const auto& item : set) { // full refill sortedSetAttr from set
        sortedSetAttr.emplace_back(item);
    }
}

void TAttrs::AppendToDictAttr(jinja2::ValuesMap& dictAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    for (const auto& value : values) {
        auto keyval = std::string_view(value.asString());
        if (auto pos = keyval.find_first_of('='); pos == std::string_view::npos) {
            spdlog::error("trying to add invalid element {} to dict {}, each element must be in key=value format without spaces around =", keyval, getDebugStr());
        } else {
            auto key = keyval.substr(0, pos);
            auto keyAttrType = GetAttrType(attr.str() + std::string{ATTR_DIVIDER} + std::string{key});
            auto val = GetSimpleAttrValue(keyAttrType, jinja2::ValuesList{std::string{keyval.substr(pos + 1)}}, getDebugStr);
            if (val.isEmpty()) {
                continue;
            }
            dictAttr.insert_or_assign(std::string{key}, std::move(val));
        }
    }
}

EAttrTypes TAttrs::GetAttrType(const std::string_view attrName) const {
    if (const auto it = AttrGroup_.find(attrName); it != AttrGroup_.end()) {
        return it->second;
    }
    return EAttrTypes::Unknown;
}

EAttrTypes TAttrs::GetItemAttrType(const std::string_view attrName) const {
    return GetAttrType(std::string{attrName} + ITEM_SUFFIX);
}

jinja2::Value TAttrs::GetSimpleAttrValue(const EAttrTypes attrType, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) {
    switch (attrType) {
        case EAttrTypes::Str:
            if (values.size() > 1) {
                spdlog::error("trying to add {} elements to 'str' type {}, type 'str' should have only 1 element", values.size(), getDebugStr());
                // but continue and use first item
            }
            return values.empty() ? std::string{} : values[0].asString();
        case EAttrTypes::Bool:
            if (values.size() > 1) {
                spdlog::error("trying to add {} elements to 'bool' type {}, type 'bool' should have only 1 element", values.size(), getDebugStr());
                // but continue and use first item
            }
            return values.empty() ? false : IsTrue(values[0].asString());
        case EAttrTypes::Flag:
            if (values.size() > 0) {
                spdlog::error("trying to add {} elements to 'flag' type {}, type 'flag' should have only 0 element", values.size(), getDebugStr());
                // but continue
            }
            return true;
        default:
            spdlog::error("try get simple value of {} with type {}", ToString<EAttrTypes>(attrType), getDebugStr());
            return {};// return empty value
    }
}

}
