#pragma once

#include "diag/exception.h"
#include "generator_spec.h"
#include "export_file_manager.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>

#include <util/generic/hash_set.h>

namespace NYexport {

    class TTargetAttributes {
    public:
        static TSimpleSharedPtr<TTargetAttributes> Create(const TAttributeGroup& attrGroup, const std::string& target);
        TTargetAttributes(const TAttributeGroup& attrGroup, const std::string& name);

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
        jinja2::ValuesMap& GetWritableMap();

    private:
        template <typename T>
        jinja2::Value ToJinjaValue(const T& value) {
            return jinja2::Value(value);
        }

        template <>
        jinja2::Value ToJinjaValue(const TVector<std::string>& value);

        bool ValidateAttrOperation(const std::string& attr, const jinja2::Value& value);

        TAttributeGroup AttrGroup;
        jinja2::ValuesMap ValueMap;
        std::string Target;
    };
    using TTargetAttributesPtr = TSimpleSharedPtr<TTargetAttributes>;

    class TJinjaTemplate {
    public:
        TJinjaTemplate() = default;

        void SetValueMap(TTargetAttributesPtr valueMap);
        bool Load(const fs::path& templatePath, jinja2::TemplateEnv* env, const std::string& renderBasename = {});
        fs::path RenderFilename(const fs::path& relativeToExportRootDirname, const std::string& platformName) const;
        bool RenderTo(TExportFileManager& exportFileManager, const fs::path& relativeToExportRootDirname = {}, const std::string& platformName = {});

        TTargetAttributes& GetValueMap();

    private:
        TTargetAttributesPtr ValueMap;
        std::optional<jinja2::Template> Template;
        std::string RenderBasename;
    };

    std::vector<TJinjaTemplate> LoadJinjaTemplates(const fs::path& templatesDir, jinja2::TemplateEnv* env, const std::vector<TTemplate>& templateSpecs);

    void Dump(IOutputStream& out, const jinja2::Value& value, int depth = 0, bool isLastItem = true);
    std::string Dump(const jinja2::Value& value, int depth = 0);

    jinja2::Value ParseValue(const toml::value& value);
    jinja2::ValuesMap ParseTable(const toml::table& table);
    jinja2::ValuesList ParseArray(const toml::array& array);
}
