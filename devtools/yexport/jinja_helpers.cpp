#include "jinja_helpers.h"

#include <spdlog/spdlog.h>

#include <util/generic/set.h>
#include <util/string/builder.h>

#include <fstream>

namespace {
    using JinjaVariant = jinja2::Value::ValueData;
    using JinjaList = jinja2::ValuesList;
    using JinjaDict = jinja2::ValuesMap;
    using JinjaListWrapper = jinja2::RecWrapper<JinjaList>;
    using JinjaDictWrapper = jinja2::RecWrapper<JinjaDict>;
    using JinjaEmpty = jinja2::EmptyValue;
    using Value = jinja2::Value;

    template <typename VariantType, typename T, std::size_t index = 0>
    constexpr std::size_t VariantIndex() {
        static_assert(std::variant_size_v<VariantType> != index, "Type not found in variant");
        if constexpr (std::is_same_v<std::variant_alternative_t<index, VariantType>, T>) {
            return index;
        } else {
            return VariantIndex<VariantType, T, index + 1>();
        }
    }

    size_t IndexByAttrType(NYexport::EAttrTypes type) {
        switch (type) {
            case NYexport::EAttrTypes::Bool:
            case NYexport::EAttrTypes::Flag:
                return VariantIndex<JinjaVariant, bool>();
            case NYexport::EAttrTypes::Str:
                return VariantIndex<JinjaVariant, std::string>();
            case NYexport::EAttrTypes::List:
            case NYexport::EAttrTypes::Set:
            case NYexport::EAttrTypes::SortedSet:
                return VariantIndex<JinjaVariant, JinjaListWrapper>();
            case NYexport::EAttrTypes::Dict:
                return VariantIndex<JinjaVariant, JinjaDictWrapper>();
            case NYexport::EAttrTypes::Unknown:
                return VariantIndex<JinjaVariant, JinjaEmpty>();
            default:
                YEXPORT_THROW("Unsupported attr type: " << type);
        }
    }

    const auto& AsString(const Value* value) {
        YEXPORT_VERIFY(value, "nullptr value in AsString");
        YEXPORT_VERIFY((value->data().index() == VariantIndex<JinjaVariant, std::string>()), "Value is not a string in AsString");

        return value->asString();
    }

    auto& AsList(Value* value) {
        YEXPORT_VERIFY(value, "nullptr value in AsList");
        YEXPORT_VERIFY((value->data().index() == VariantIndex<JinjaVariant, JinjaListWrapper>()), "Value is not a list in AsList");

        return value->asList();
    }

    const auto& AsList(const Value* value) {
        YEXPORT_VERIFY(value, "nullptr value in AsList");
        YEXPORT_VERIFY((value->data().index() == VariantIndex<JinjaVariant, JinjaListWrapper>()), "Value is not a list in AsList");

        return value->asList();
    }

    auto& AsDict(Value* value) {
        YEXPORT_VERIFY(value, "nullptr value in AsDict");
        YEXPORT_VERIFY((value->data().index() == VariantIndex<JinjaVariant, JinjaDictWrapper>()), "Value is not a dict in AsDict");

        return value->asMap();
    }

    const auto& AsDict(const Value* value) {
        YEXPORT_VERIFY(value, "nullptr value in AsDict");
        YEXPORT_VERIFY((value->data().index() == VariantIndex<JinjaVariant, JinjaDictWrapper>()), "Value is not a dict in AsDict");

        return value->asMap();
    }

    void AppendListLikeAttr(jinja2::Value& attr, NYexport::EAttrTypes type, const jinja2::Value& value) {
        YEXPORT_VERIFY((attr.data().index() == VariantIndex<JinjaVariant, JinjaListWrapper>()), "AppendListAttr on non list like attr");
        const auto& values = AsList(&value);
        auto& attrList = AsList(&attr);

        TSet<std::string> valuesSet;
        switch (type) {
            case NYexport::EAttrTypes::List:
                attrList.insert(attrList.begin(), values.begin(), values.end());
                break;
            case NYexport::EAttrTypes::Set:
                for (const auto& val : attrList) {
                    valuesSet.emplace(AsString(&val));
                }
                for (const auto& val : values) {
                    const auto& strVal = AsString(&val);
                    if (valuesSet.emplace(strVal).second) {
                        attrList.push_back(strVal);
                    }
                }
                break;
            case NYexport::EAttrTypes::SortedSet:
                for (const auto& val : attrList) {
                    valuesSet.emplace(AsString(&val));
                }
                for (const auto& val : values) {
                    valuesSet.emplace(AsString(&val));
                }
                attrList.clear();
                for (const auto& val : valuesSet) {
                    attrList.emplace_back(val);
                }
                break;
            default:
                YEXPORT_THROW("Wrong attr type in AppendListLikeAttr");
        }
    }

    void SetListLikeValue(jinja2::Value& attr, NYexport::EAttrTypes type, const jinja2::Value& value) {
        YEXPORT_VERIFY((attr.data().index() == VariantIndex<JinjaVariant, JinjaListWrapper>()), "SetListLikeValue on non list like attr");
        auto& attrList = AsList(&attr);
        attrList.clear();
        AppendListLikeAttr(attr, type, value);
    }

    void AppendDictAttr(jinja2::Value& attr, const jinja2::Value& value) {
        YEXPORT_VERIFY((attr.data().index() == VariantIndex<JinjaVariant, JinjaDictWrapper>()), "AppendDictAttr on non dict attr");
        const auto& values = AsDict(&value);
        auto& attrDict = AsDict(&attr);
        attrDict.insert(values.begin(), values.end());
    }

    void SetDictValue(jinja2::Value& attr, const jinja2::Value& value) {
        YEXPORT_VERIFY((attr.data().index() == VariantIndex<JinjaVariant, JinjaDictWrapper>()), "AppendDictAttr on non dict attr");
        auto& attrDict = AsDict(&attr);
        attrDict.clear();
        AppendDictAttr(attr, value);
    }

}

namespace NYexport {

    TSimpleSharedPtr<TTargetAttributes> TTargetAttributes::Create(const TAttributeGroup& attrGroup, const std::string& name) {
        return MakeSimpleShared<TTargetAttributes>(attrGroup, name);
    }

    TTargetAttributes::TTargetAttributes(const TAttributeGroup& attrGroup, const std::string& name) : AttrGroup(attrGroup), Target(name) {
        for (const auto& [attr, type] : AttrGroup) {
            AttrGroup[attr] = type;
            auto typeIndex = IndexByAttrType(type);
            if (typeIndex == VariantIndex<JinjaVariant, JinjaEmpty>()) {
                spdlog::error("Attribute [{}] has no type and will not be created", attr.str());
            }
            switch (typeIndex) {
                case VariantIndex<JinjaVariant, bool>():
                    ValueMap[attr] = false;
                    break;
                case VariantIndex<JinjaVariant, std::string>():
                    ValueMap[attr] = "";
                    break;
                case VariantIndex<JinjaVariant, JinjaListWrapper>():
                    ValueMap[attr] = JinjaList();
                    break;
                case VariantIndex<JinjaVariant, JinjaDictWrapper>():
                    ValueMap[attr] = JinjaDict();
                    break;
                default:
                    YEXPORT_THROW(TBadGeneratorSpec(), "Unsupported attribute type: " << type);
            }
        }
    }

    template <>
    jinja2::Value TTargetAttributes::ToJinjaValue(const TVector<std::string>& value) {
        return jinja2::ValuesList(value.begin(), value.end());
    }

    template <>
    bool TTargetAttributes::SetAttrValue(const std::string& attr, const jinja2::Value& value) {
        if (!ValidateAttrOperation(attr, value)) {
            return false;
        }
        auto type = AttrGroup.at(attr);
        auto valueIndex = IndexByAttrType(type);
        switch (valueIndex) {
            case VariantIndex<JinjaVariant, bool>():
            case VariantIndex<JinjaVariant, std::string>():
                ValueMap[attr] = value;
                break;
            case VariantIndex<JinjaVariant, JinjaListWrapper>():
                SetListLikeValue(ValueMap[attr], type, value);
                break;
            case VariantIndex<JinjaVariant, JinjaDictWrapper>():
                SetDictValue(ValueMap[attr], value);
                break;
            default:
                YEXPORT_THROW(TBadGeneratorSpec(), "Unsupported attribute type: " << type);
        }
        return true;
    }
    template <>
    bool TTargetAttributes::AppendAttrValue(const std::string& attr, const jinja2::Value& value) {
        if (!ValidateAttrOperation(attr, value)) {
            return false;
        }
        auto type = AttrGroup.at(attr);
        auto valueIndex = IndexByAttrType(type);
        switch (valueIndex) {
            case VariantIndex<JinjaVariant, bool>():
            case VariantIndex<JinjaVariant, std::string>():
                YEXPORT_THROW("Cannot append to bool or string");
                break;
            case VariantIndex<JinjaVariant, JinjaListWrapper>():
                AppendListLikeAttr(ValueMap[attr], type, value);
                break;
            case VariantIndex<JinjaVariant, JinjaDictWrapper>():
                AppendDictAttr(ValueMap[attr], value);
                break;
            default:
                YEXPORT_THROW(TBadGeneratorSpec(), "Unsupported attribute type: " << type);
        }
        return true;
    }

    const jinja2::ValuesMap& TTargetAttributes::GetMap() const {
        return ValueMap;
    }

    jinja2::ValuesMap& TTargetAttributes::GetWritableMap() {
        return ValueMap;
    }

    bool TTargetAttributes::ValidateAttrOperation(const std::string& attr, const jinja2::Value& value) {
        auto typeIt = AttrGroup.find(attr);
        if (typeIt == AttrGroup.end()) {
            spdlog::error("Trying to perform operation on unknown attribute [{}] in valueMap [{}]", attr, Target);
            return false;
        }
        auto attrIt = ValueMap.find(attr);
        auto type = typeIt->second;
        auto actualValueIndex = value.data().index();
        auto expectedValueIndex = IndexByAttrType(type);
        YEXPORT_VERIFY(attrIt != ValueMap.end(), "Internal error in JinjaTemplate: attribute is defined but has no value");
        YEXPORT_VERIFY(attrIt->second.data().index() == expectedValueIndex, "Internal error in JinjaTemplate: attribute's value type does not match defined type");

        if (actualValueIndex != expectedValueIndex) {
            spdlog::error("Trying to perform operation with value type index {} while actual value type index is {} in valueMap {}: attribute = {}", actualValueIndex, expectedValueIndex, Target, attr);
            return false;
        }
        return true;
    }

    bool TJinjaTemplate::Load(const fs::path& path, jinja2::TemplateEnv* env) {
        Template = {};

        std::ifstream file(path);
        YEXPORT_VERIFY(file.good(), "Failed to open jinja template file: " << path.c_str());

        jinja2::Template tmpl(env);
        auto res = tmpl.Load(file, path.c_str());
        if (!res.has_value()) {
            spdlog::error("Failed to load jinja template due: {}", res.error().ToString());
            return false;
        }

        Template = std::move(tmpl);
        return true;
    }

    bool TJinjaTemplate::RenderTo(TExportFileManager& exportFileManager, const fs::path& relativeToExportRoot) {
        if (!Template || !ValueMap) {
            return false;
        }
        jinja2::Result<TString> result = Template->RenderAsString(ValueMap->GetMap());

        if (!result.has_value()) {
            spdlog::error("Failed to render {} due to jinja template error: {}", relativeToExportRoot.c_str(), result.error().ToString());
            return false;
        }
        auto out = exportFileManager.Open(relativeToExportRoot);
        TString renderResult = result.value();
        out.Write(renderResult.data(), renderResult.size());
        return true;
    }

    void TJinjaTemplate::SetValueMap(TTargetAttributesPtr valueMap) {
        ValueMap = valueMap;
    }

    void Dump(IOutputStream& out, const jinja2::Value& value, int depth, bool isLastItem) {
        auto Indent = [](int depth) {
            return std::string(depth * 4, ' ');
        };
        if (value.isMap()) {
            if (!depth) out << Indent(depth);
            out << "{\n";
            const auto& map = value.asMap();
            if (!map.empty()) {
                // Sort map keys before dumping
                TVector<std::string> keys;
                keys.reserve(map.size());
                for (const auto& [key, _] : map) {
                    keys.emplace_back(key);
                }
                Sort(keys);
                for (const auto& key : keys) {
                    out << Indent(depth + 1) << key << ": ";
                    const auto it = map.find(key);
                    Dump(out, it->second, depth + 1, &key == &keys.back());
                }
            }
            out << Indent(depth) << "}";
        } else if (value.isList()) {
            out << "[\n";
            const auto& vals = value.asList();
            for (const auto& val : vals) {
                out << Indent(depth + 1);
                Dump(out, val, depth + 1, &val == &vals.back());
            }
            out << Indent(depth) << "]";
        }  else if (value.isString()) {
            out << '"' << value.asString() << '"';
        } else if (value.isEmpty()) {
            out << "EMPTY";
        } else {
            if (std::holds_alternative<bool>(value.data())) {
                out << (value.get<bool>() ? "true" : "false");
            } else if (std::holds_alternative<int64_t>(value.data())) {
                out << value.get<int64_t>();
            } else if (std::holds_alternative<double>(value.data())) {
                out << value.get<double>();
            } else {
                out << "???";
            }
        }
        if (!isLastItem) {
            out << ",";
        }
        out << "\n";
    }

    std::string Dump(const jinja2::Value& value, int depth) {
        TStringBuilder strBuilder;
        Dump(strBuilder.Out, value, depth);
        if (strBuilder.empty()) {
            return {};
        }
        return strBuilder;
    }


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
