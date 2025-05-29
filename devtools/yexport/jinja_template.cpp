#include "jinja_template.h"
#include "stat.h"

#include <spdlog/spdlog.h>

#include <fstream>

namespace {
    const std::string PLATFORM_IN_FILENAME = "{PLATFORM}";
}

namespace NYexport {
    bool TJinjaTemplate::Load(const fs::path& path, jinja2::TemplateEnv* env, const std::string& renderBasename) {
        TStageCall stage("load>jinja");
        Template = {};

        std::ifstream file(path);
        YEXPORT_VERIFY(file.good(), "Failed to open jinja template file: " << path.c_str());

        jinja2::Template jinjaTemplate(env);
        auto res = jinjaTemplate.Load(file, path.c_str());
        if (!res.has_value()) {
            spdlog::error("Failed to load jinja template {} due: {}", path.c_str(), res.error().ToString());
            return false;
        }

        Template = std::move(jinjaTemplate);
        if (renderBasename.empty()) {
            auto filename = path.filename();
            if (filename.extension() == ".jinja") {
                filename.replace_extension("");
            }
            RenderBasename = filename.string();
        } else {
            RenderBasename = renderBasename;
        }
        return true;
    }

    fs::path TJinjaTemplate::RenderFilename(const fs::path& relativeToExportRootDirname, const std::string& platformName) const {
        auto platformPos = RenderBasename.find(PLATFORM_IN_FILENAME);
        TFile out;
        if (std::string::npos == platformPos) {
            return relativeToExportRootDirname / RenderBasename;
        } else {
            const std::string platform = platformName.empty() ? platformName : "." + platformName;
            auto renderBasename = RenderBasename.substr(0, platformPos) + platform + RenderBasename.substr(platformPos + PLATFORM_IN_FILENAME.size());
            return relativeToExportRootDirname / renderBasename;
        }
    }

    jinja2::Result<TString> TJinjaTemplate::RenderAsString() {
        TStageCall stage("render>jinja");
        return Template->RenderAsString(ValueMap->GetMap());
    }

    bool TJinjaTemplate::RenderTo(TExportFileManager& exportFileManager, const fs::path& relativeToExportRootDirname, const std::string& platformName) {
        if (!Template || !ValueMap) {
            return false;
        }
        auto result = RenderAsString();
        auto renderFileName = RenderFilename(relativeToExportRootDirname, platformName);
        if (!result.has_value()) {
            spdlog::error("Failed to render {} due to jinja template error: {}", renderFileName.c_str(), result.error().ToString());
            return false;
        }
        exportFileManager.Save(renderFileName, result.value());
        return true;
    }

    void TJinjaTemplate::SetValueMap(TAttrsPtr valueMap) {
        ValueMap = valueMap;
    }

    std::vector<TJinjaTemplate> LoadJinjaTemplates(const fs::path& templatesDir, jinja2::TemplateEnv* env, const std::vector<TTemplateSpec>& templateSpecs) {
        if (templateSpecs.empty()) {
            return {};
        }
        std::vector<TJinjaTemplate> jinjaTemplates;
        jinjaTemplates.reserve(templateSpecs.size());
        for (const auto& templateSpec: templateSpecs) {
            TJinjaTemplate t;
            if (t.Load(templatesDir / templateSpec.Template, env, templateSpec.ResultName)) {
                jinjaTemplates.emplace_back(std::move(t));
            }
        }
        return jinjaTemplates;
    }
}
