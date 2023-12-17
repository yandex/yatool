#include "jinja_helpers.h"

#include <spdlog/spdlog.h>

#include <util/generic/set.h>

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

    TSimpleSharedPtr<TTargetAttributes> TTargetAttributes::Create(const TAttrsSpec& attrSpec, const std::string& name) {
        return MakeSimpleShared<TTargetAttributes>(attrSpec, name);
    }

    TTargetAttributes::TTargetAttributes(const TAttrsSpec& attrSpec, const std::string& name) : Target(name) {
        for (const auto& [attr, spec] : attrSpec.Items) {
            AttrTypes[attr] = spec.Type;
            auto typeIndex = IndexByAttrType(spec.Type);
            if (typeIndex == VariantIndex<JinjaVariant, JinjaEmpty>()) {
                spdlog::error("Attribute [{}] has no type and will not be created", attr);
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
                    YEXPORT_THROW(TBadGeneratorSpec(), "Unsupported attribute type: " << spec.Type);
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
        auto type = AttrTypes.at(attr);
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
        auto type = AttrTypes.at(attr);
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

    bool TTargetAttributes::ValidateAttrOperation(const std::string& attr, const jinja2::Value& value) {
        auto typeIt = AttrTypes.find(attr);
        if (typeIt == AttrTypes.end()) {
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

    bool TJinjaTemplate::Load(const fs::path& path) {
        Template = {};

        std::ifstream file(path);
        YEXPORT_VERIFY(file.good(), "Failed to open jinja template file: " << path.c_str());

        jinja2::Template tmpl;
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
}
