#pragma once

#include "attributes.h"
#include "export_file_manager.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>

namespace NYexport {
    class TJinjaTemplate {
    public:
        TJinjaTemplate() = default;

        void SetValueMap(TAttrsPtr valueMap);
        bool Load(const fs::path& templatePath, jinja2::TemplateEnv* env, const std::string& renderBasename = {});
        fs::path RenderFilename(const fs::path& relativeToExportRootDirname, const std::string& platformName) const;
        bool RenderTo(TExportFileManager& exportFileManager, const fs::path& relativeToExportRootDirname = {}, const std::string& platformName = {});

        TAttrs& GetValueMap();

    private:
        TAttrsPtr ValueMap;
        std::optional<jinja2::Template> Template;
        std::string RenderBasename;

        jinja2::Result<TString> RenderAsString();
    };

    std::vector<TJinjaTemplate> LoadJinjaTemplates(const fs::path& templatesDir, jinja2::TemplateEnv* env, const std::vector<TTemplateSpec>& templateSpecs);
}
