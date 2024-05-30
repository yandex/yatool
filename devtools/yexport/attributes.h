#pragma once

#include "generator_spec.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>

template<typename Values>
concept IterableValues = std::ranges::range<Values>;

namespace NYexport {

    class TAttrs {
    public:
        using TReplacer = std::function<const std::string&(const std::string&)>;

        static TSimpleSharedPtr<TAttrs> Create(const TAttrGroup& attrGroup, const std::string& name, const TReplacer* replacer = nullptr, const TReplacer* toolGetter = nullptr);
        TAttrs(const TAttrGroup& attrGroup, const std::string& name, const TReplacer* replacer = nullptr, const TReplacer* toolGetter = nullptr);

        template<IterableValues Values>
        void SetAttrValue(const std::string_view attrName, const Values& values, const std::string& nodePath) {
            TAttr attr(attrName);
            // Convert values to jinja2::ValuesList
            jinja2::ValuesList jvalues;
            const jinja2::ValuesList* valuesPtr;
            if constexpr(std::same_as<Values, jinja2::ValuesList>) {
                valuesPtr = &values;
            } else {
                Copy(values.begin(), values.end(), std::back_inserter(jvalues));
                valuesPtr = &jvalues;
            }
            SetAttrValue(attr, *valuesPtr, nodePath);
        }

        void SetAttrValue(const TAttr& attr, const jinja2::ValuesList& values, const std::string& nodePath) {
            SetAttrValue(Attrs_, attr, values, 0, [&]() {
                return "attribute " + attr.str() + " of " + Name_ + " at " + nodePath;// debug string for error messages
            });
        }

        const jinja2::ValuesMap& GetMap() const { return Attrs_; }
        jinja2::ValuesMap& GetWritableMap() { return Attrs_; }

    private:
        using TGetDebugStr = const std::function<std::string()>&;

        const TAttrGroup& AttrGroup_;
        const std::string Name_;
        jinja2::ValuesMap Attrs_;
        const TReplacer* Replacer_;
        const TReplacer* ToolGetter_;

        void SetAttrValue(jinja2::ValuesMap& attrs, const TAttr& attr, const jinja2::ValuesList& values, size_t atPos, TGetDebugStr getDebugStr);

        void SetStrAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const jinja2::Value& value, TGetDebugStr getDebugStr) const;
        void SetBoolAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const jinja2::Value& value, TGetDebugStr getDebugStr) const;
        void SetFlagAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const jinja2::Value& value, TGetDebugStr getDebugStr) const;

        void AppendToListAttr(jinja2::ValuesList& listAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr);
        void AppendToSetAttr(jinja2::ValuesList& setAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr);
        void AppendToSortedSetAttr(jinja2::ValuesList& sortedSetAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr);
        void AppendToDictAttr(jinja2::ValuesMap& dictAttr, const TAttr& attr, const jinja2::ValuesList& values, TGetDebugStr getDebugStr);

        EAttrTypes GetItemAttrType(const std::string_view attrName) const;
        EAttrTypes GetAttrType(const std::string_view attrName) const;
        jinja2::Value GetSimpleAttrValue(const EAttrTypes attrType, const jinja2::ValuesList& values, TGetDebugStr getDebugStr);
    };

    using TAttrsPtr = TSimpleSharedPtr<TAttrs>;
}
