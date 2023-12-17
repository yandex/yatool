#pragma once

#include "diag/exception.h"
#include "generator_spec.h"
#include "export_file_manager.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>

#include <util/generic/hash_set.h>

namespace NYexport {
    namespace fs = std::filesystem;

    class TTargetAttributes {
    public:
        static TSimpleSharedPtr<TTargetAttributes> Create(const TAttrsSpec& attrSpec, const std::string& target);
        TTargetAttributes(const TAttrsSpec& attrSpec, const std::string& name);

        template <typename T>
        bool SetAttrValue(const std::string& attr, const T& value) {
            return SetAttrValue(attr, ToJinjaValue(value));
        }
        template <>
        bool SetAttrValue(const std::string& attr, const jinja2::Value& value);

        template <typename T>
        bool AppendAttrValue(const std::string& attr, const T& value) {
            return AppendAttrValue(attr, ToJinjaValue(value));
        }
        template <>
        bool AppendAttrValue(const std::string& attr, const jinja2::Value& value);

        const jinja2::ValuesMap& GetMap() const;

    private:
        template <typename T>
        jinja2::Value ToJinjaValue(const T& value) {
            return jinja2::Value(value);
        }

        template <>
        jinja2::Value ToJinjaValue(const TVector<std::string>& value);

        bool ValidateAttrOperation(const std::string& attr, const jinja2::Value& value);

        THashMap<std::string, EAttrTypes> AttrTypes;
        jinja2::ValuesMap ValueMap;
        std::string Target;
    };
    using TTargetAttributesPtr = TSimpleSharedPtr<TTargetAttributes>;

    class TJinjaTemplate {
    public:
        TJinjaTemplate() = default;
        TJinjaTemplate(const fs::path& templatePath);

        void SetValueMap(TTargetAttributesPtr valueMap);
        bool Load(const fs::path& templatePath);
        bool RenderTo(TExportFileManager& exportFileManager, const fs::path& relativeToExportRoot);

        TTargetAttributes& GetValueMap();
    private:
        TTargetAttributesPtr ValueMap;
        std::optional<jinja2::Template> Template;
    };
}
