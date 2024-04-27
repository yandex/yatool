#include "spec_based_generator.h"
#include "yexport_spec.h"

#include <spdlog/spdlog.h>

namespace {

    class TPrefixedFileSystem: public jinja2::IFilesystemHandler {
    public:
        TPrefixedFileSystem(const std::string& prefix)
            : Prefix_(prefix)
        {
        }

        jinja2::CharFileStreamPtr OpenStream(const std::string& name) const override {
            std::string realName(CutPrefix(name));
            return Filesystem_.OpenStream(realName);
        };
        jinja2::WCharFileStreamPtr OpenWStream(const std::string& name) const override {
            std::string realName(CutPrefix(name));
            return Filesystem_.OpenWStream(realName);
        };
        std::optional<std::chrono::system_clock::time_point> GetLastModificationDate(const std::string& name) const override {
            std::string realName(CutPrefix(name));
            return Filesystem_.GetLastModificationDate(realName);
        };

        void SetRootFolder(const fs::path& path) {
            Filesystem_.SetRootFolder(path);
        }

        bool IsEqual(const IComparable& other) const override {
            return Filesystem_.IsEqual(other);
        }

    private:
        std::string_view CutPrefix(const std::string& name) const {
            std::string_view sv = name;
            if (sv.find(Prefix_) == 0) {
                sv.remove_prefix(Prefix_.size());
            }
            return sv;
        }

        jinja2::RealFileSystem Filesystem_;
        const std::string Prefix_;
    };
}

namespace NYexport {

const TGeneratorSpec& TSpecBasedGenerator::GetGeneratorSpec() const {
    return GeneratorSpec;
}

const fs::path& TSpecBasedGenerator::GetGeneratorDir() const {
    return GeneratorDir;
}

void TSpecBasedGenerator::OnAttribute(const TAttr& attribute) {
    for (size_t i = 0; i < attribute.Size(); ++i) {
        std::string attr(attribute.GetFirstParts(i));
        UsedAttributes.emplace(attr);
        auto rules = GeneratorSpec.GetAttrRules(attr);
        UsedRules.insert(rules.begin(), rules.end());
    }
}

void TSpecBasedGenerator::OnPlatform(const std::string_view& platform) {
    auto rules = GeneratorSpec.GetPlatformRules(platform);
    UsedRules.insert(rules.begin(), rules.end());
}

void TSpecBasedGenerator::ApplyRules(TAttrsPtr attrs) const {
    for (const auto& rule : UsedRules) {
        for (const auto& [attr, values] : rule->AddValues) {
            attrs->SetAttrValue(attr, values, "apply_rules");
        }
    }
}

jinja2::TemplateEnv* TSpecBasedGenerator::GetJinjaEnv() const {
    return JinjaEnv.get();
}

void TSpecBasedGenerator::SetupJinjaEnv() {
    JinjaEnv = std::make_unique<jinja2::TemplateEnv>();
    JinjaEnv->GetSettings().cacheSize = 0;// REQUIRED for use templates with same relative path, jinja2cpp caching by path without root
    SourceTemplateFs = std::make_shared<jinja2::RealFileSystem>();
    auto GeneratorTemplateFs = std::make_shared<TPrefixedFileSystem>(GENERATOR_TEMPLATES_PREFIX);
    GeneratorTemplateFs->SetRootFolder(GeneratorDir);
    SourceTemplateFs->SetRootFolder(ArcadiaRoot);

    // Order matters. Jinja should search files with generator/ prefix in generator directory, and only if there is no such file fallback to source directory
    JinjaEnv->AddFilesystemHandler(GENERATOR_TEMPLATES_PREFIX, GeneratorTemplateFs);
    JinjaEnv->AddFilesystemHandler({}, SourceTemplateFs);

    // Handmade split function
    JinjaEnv->AddGlobal("split", jinja2::UserCallable{
        /*fptr=*/[](const jinja2::UserCallableParams& params) -> jinja2::Value {
            Y_ASSERT(params["str"].isString());
            auto str = params["str"].asString();
            Y_ASSERT(params["delimeter"].isString());
            auto delimeter = params["delimeter"].asString();
            jinja2::ValuesList list;
            size_t bpos = 0;
            size_t dpos;
            while ((dpos = str.find(delimeter, bpos)) != std::string::npos) {
                list.emplace_back(str.substr(bpos, dpos - bpos));
                bpos = dpos + delimeter.size();
            }
            list.emplace_back(str.substr(bpos));
            return list;
        },
        /*argsInfos=*/ { jinja2::ArgInfo{"str"}, jinja2::ArgInfo{"delimeter", false, " "} }
    });
}

void TSpecBasedGenerator::SetCurrentDirectory(const fs::path& dir) const {
    YEXPORT_VERIFY(JinjaEnv, "Cannot set current directory to " << dir << " before setting up jinja enviroment");
    SourceTemplateFs->SetRootFolder(dir.c_str());
}


const TNodeSemantics& TSpecBasedGenerator::ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
    return TargetReplacements_.ApplyReplacement(path, inputSem);
}

TYexportSpec TSpecBasedGenerator::ReadYexportSpec(fs::path configDir) {
    if (!configDir.empty()) {
        auto yexportToml = configDir / YEXPORT_FILE;
        if (fs::exists(yexportToml)) {
            LoadTargetReplacements(yexportToml, TargetReplacements_);
            return ::NYexport::ReadYexportSpec(yexportToml);
        }
    }
    return {};
}

TAttrsPtr TSpecBasedGenerator::MakeAttrs(EAttrGroup eattrGroup, const std::string& name) const {
    const auto attrGroupIt = GeneratorSpec.AttrGroups.find(eattrGroup);
    if (attrGroupIt != GeneratorSpec.AttrGroups.end()) {
        return TAttrs::Create(attrGroupIt->second, name);
    } else {
        static const TAttrGroup EMPTY_ATTR_GROUP;
        spdlog::error("No attribute specification for {}", ToString<EAttrGroup>(eattrGroup));
        return TAttrs::Create(EMPTY_ATTR_GROUP, name);
    }
}

bool TSpecBasedGenerator::IgnorePlatforms() const {
    return GeneratorSpec.IgnorePlatforms;
}

fs::path TSpecBasedGenerator::PathByCopyLocation(ECopyLocation location) const {
    switch (location) {
        case ECopyLocation::GeneratorRoot:
            return GeneratorDir;
        case ECopyLocation::SourceRoot:
            return ArcadiaRoot;
        default:
            YEXPORT_THROW("Unknown copy location");
    }
}

TCopySpec TSpecBasedGenerator::CollectFilesToCopy() const {
    TCopySpec result;

    for (auto rule : UsedRules) {
        result.Append(rule->Copy);
    }
    result.Append(GeneratorSpec.Root.Copy);
    return result;
}

void TSpecBasedGenerator::CopyFilesAndResources() {
    for (const auto& [location, files] : CollectFilesToCopy().Items) {
        auto dir = PathByCopyLocation(location);
        for (const auto& file : files) {
            ExportFileManager->Copy(dir / file, file);
        }
    }
}

std::vector<TJinjaTemplate> TSpecBasedGenerator::LoadJinjaTemplates(const std::vector<TTemplate>& templateSpecs) const {
    return NYexport::LoadJinjaTemplates(GetGeneratorDir(), GetJinjaEnv(), templateSpecs);
}

void TSpecBasedGenerator::RenderJinjaTemplates(TAttrsPtr attrs, std::vector<TJinjaTemplate>& jinjaTemplates, const fs::path& relativeToExportRootDirname, const std::string& platformName) {
    for (auto& jinjaTemplate: jinjaTemplates) {
        jinjaTemplate.SetValueMap(attrs);
        jinjaTemplate.RenderTo(*ExportFileManager, relativeToExportRootDirname, platformName);
    }
}

void TSpecBasedGenerator::MergePlatforms(const std::vector<TJinjaTemplate>& dirTemplates, std::vector<TJinjaTemplate>& commonTemplates) const {
    if (commonTemplates.empty()) { // can't merge platforms without common templates
        return;
    }

    THashMap<fs::path, std::vector<TPlatformPtr>> dir2platforms;// list of platforms for each dir
    for (const auto& platform : Platforms) {
        for (const auto& dir : platform->Project->GetSubdirs()) {
            dir2platforms[dir->Path].emplace_back(platform);
        }
    }

    Y_ASSERT(commonTemplates.size() == dirTemplates.size());
    auto templatesCount = dirTemplates.size();
    for (size_t i = 0; i < templatesCount; ++i) {
        const auto& dirTemplate = dirTemplates[i];
        auto& commonTemplate = commonTemplates[i];
        for (const auto& [dir, dirPlatforms]: dir2platforms) {
            bool isDifferent = false;
            if (dirPlatforms.size() > 1) {
                TString md5 = ExportFileManager->MD5(dirTemplate.RenderFilename(dir, dirPlatforms[0]->Name));
                for (size_t j = 1; j < dirPlatforms.size(); ++j) {
                    TString otherMd5 = ExportFileManager->MD5(dirTemplate.RenderFilename(dir, dirPlatforms[j]->Name));
                    if (isDifferent |= (md5 != otherMd5)) {
                        break;
                    }
                }
            }
            if (isDifferent) {
                TAttrsPtr dirValueMap = MakeAttrs(EAttrGroup::Directory, dir);
                auto& dirMap = dirValueMap->GetWritableMap();
                dirMap["platforms"] = GeneratorSpec.Platforms;
                InsertPlatforms(dirMap, dirPlatforms);
                SetCurrentDirectory(ArcadiaRoot / dir);
                commonTemplate.SetValueMap(dirValueMap);
                commonTemplate.RenderTo(*ExportFileManager, dir);
            } else {
                auto finalPath = commonTemplate.RenderFilename(dir, "");
                ExportFileManager->CopyFromExportRoot(dirTemplate.RenderFilename(dir, dirPlatforms[0]->Name), finalPath);
                for (const auto& dirPlatform : dirPlatforms) {
                    ExportFileManager->Remove(dirTemplate.RenderFilename(dir, dirPlatform->Name));
                }
            }
        }
    }
}

void TSpecBasedGenerator::InsertPlatforms(jinja2::ValuesMap& valuesMap, const std::vector<TPlatformPtr>& platforms) {
    auto& platformNames = valuesMap.insert_or_assign("platform_names", jinja2::ValuesList()).first->second.asList();
    for (const auto& platform : platforms) {
        platformNames.emplace_back(platform->Name);
    }
}

}
