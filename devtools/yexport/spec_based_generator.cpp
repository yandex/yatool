#include "spec_based_generator.h"
#include "yexport_spec.h"
#include "internal_attributes.h"

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

    // Handmade map keys function
    JinjaEnv->AddGlobal("keys", jinja2::UserCallable{
        /*fptr=*/[](const jinja2::UserCallableParams& params) -> jinja2::Value {
            jinja2::ValuesList list;
            const auto arg = params["map"];
            if (arg.isMap()) {
                const auto* genMap = arg.getPtr<jinja2::GenericMap>();
                if (genMap) {// workaround for GenericMap, asMap generate bad_variant_access for it
                    auto keys = genMap->GetKeys();
                    list.insert(list.end(), keys.begin(), keys.end());
                } else {
                    for (const auto& [key, _] : arg.asMap()) {
                        list.emplace_back(key);
                    }
                }
            }
            return list;
        },
        /*argsInfos=*/ { jinja2::ArgInfo{"map"} }
    });
}

void TSpecBasedGenerator::SetCurrentDirectory(const fs::path& dir) const {
    YEXPORT_VERIFY(JinjaEnv, "Cannot set current directory to " << dir << " before setting up jinja environment");
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

std::vector<TJinjaTemplate> TSpecBasedGenerator::LoadJinjaTemplates(const std::vector<TTemplateSpec>& templateSpecs) const {
    return NYexport::LoadJinjaTemplates(GetGeneratorDir(), GetJinjaEnv(), templateSpecs);
}

void TSpecBasedGenerator::RenderJinjaTemplates(TAttrsPtr attrs, std::vector<TJinjaTemplate>& jinjaTemplates, const fs::path& relativeToExportRootDirname, const std::string& platformName) {
    for (auto& jinjaTemplate: jinjaTemplates) {
        jinjaTemplate.SetValueMap(attrs);
        jinjaTemplate.RenderTo(*ExportFileManager, relativeToExportRootDirname, platformName);
    }
}

void TSpecBasedGenerator::MergePlatforms() {
    THashSet<const TProjectSubdir*> dirs;
    THashMap<fs::path, std::vector<TPlatformPtr>> dir2platforms;// list of platforms for each dir
    for (const auto& platform : Platforms) {
        for (const auto& dir : platform->Project->GetSubdirs()) {
            const auto& mainTargetMacro = dir->MainTargetMacro.empty() ? EMPTY_TARGET : dir->MainTargetMacro;
            if (!MergePlatformTargetTemplates.contains(mainTargetMacro)) {
                continue; // ignore directories without merge platform templates
            }
            dir2platforms[dir->Path].emplace_back(platform);// collect platforms for every dir
            dirs.insert(dir.Get());// and all directories for merge platforms
        }
    }
    for (const auto* dir : dirs) {
        const auto& path = dir->Path;
        const auto& mainTargetMacro = dir->MainTargetMacro.empty() ? EMPTY_TARGET : dir->MainTargetMacro;
        const auto& dirTemplates = TargetTemplates.at(mainTargetMacro);
        auto& mergePlatformTemplates = MergePlatformTargetTemplates.at(mainTargetMacro);
        Y_ASSERT(dirTemplates.size() == mergePlatformTemplates.size());
        const auto& dirPlatforms = dir2platforms[path];
        auto templatesCount = dirTemplates.size();
        for (size_t i = 0; i < templatesCount; ++i) {
            const auto& dirTemplate = dirTemplates[i];
            auto& commonTemplate = mergePlatformTemplates[i];
            bool isDifferent = false;
            if (dirPlatforms.size() > 1) {
                TString md5 = ExportFileManager->MD5(dirTemplate.RenderFilename(path, dirPlatforms[0]->Name));
                for (size_t j = 1; j < dirPlatforms.size(); ++j) {
                    TString otherMd5 = ExportFileManager->MD5(dirTemplate.RenderFilename(path, dirPlatforms[j]->Name));
                    if (isDifferent |= (md5 != otherMd5)) {
                        break;
                    }
                }
            }
            if (isDifferent) {
                TAttrsPtr dirAttrs = MakeAttrs(EAttrGroup::Directory, path);
                InsertPlatformNames(dirAttrs, dirPlatforms);
                InsertPlatformConditions(dirAttrs, true);
                CommonFinalizeAttrs(dirAttrs, YexportSpec.AddAttrsDir);
                SetCurrentDirectory(ArcadiaRoot / path);
                commonTemplate.SetValueMap(dirAttrs);
                commonTemplate.RenderTo(*ExportFileManager, path);
            } else {
                auto finalPath = commonTemplate.RenderFilename(path, "");
                ExportFileManager->CopyFromExportRoot(dirTemplate.RenderFilename(path, dirPlatforms[0]->Name), finalPath);
                for (const auto& dirPlatform : dirPlatforms) {
                    ExportFileManager->Remove(dirTemplate.RenderFilename(path, dirPlatform->Name));
                }
            }
        }
    }
}

void TSpecBasedGenerator::InsertPlatformNames(TAttrsPtr& attrs, const std::vector<TPlatformPtr>& platforms) {
    Y_ASSERT(attrs);
    auto& map = attrs->GetWritableMap();
    jinja2::ValuesList platformNames;
    for (const auto& platform : platforms) {
        platformNames.emplace_back(platform->Name);
    }
    NInternalAttrs::EmplaceAttr(map, NInternalAttrs::PlatformNames, std::move(platformNames));
}

void TSpecBasedGenerator::InsertPlatformConditions(TAttrsPtr& attrs, bool addDeprecated) {
    Y_ASSERT(attrs);
    NInternalAttrs::EmplaceAttr(attrs->GetWritableMap(), NInternalAttrs::PlatformConditions, GeneratorSpec.Platforms);
    if (addDeprecated) {
        // DEPRECATED - remove after replace platforms to platform_conditions in all templates
        NInternalAttrs::EmplaceAttr(attrs->GetWritableMap(), "platforms", GeneratorSpec.Platforms);
    }
}

void TSpecBasedGenerator::InsertPlatformAttrs(TAttrsPtr& attrs) {
    Y_ASSERT(attrs);
    jinja2::ValuesMap platformAttrs;
    for (const auto &platform: Platforms) {
        platformAttrs.emplace(platform->Name, platform->Project->PlatformAttrs->GetMap());
    }
    NInternalAttrs::EmplaceAttr(attrs->GetWritableMap(), NInternalAttrs::PlatformAttrs, std::move(platformAttrs));
}

void TSpecBasedGenerator::CommonFinalizeAttrs(TAttrsPtr& attrs, const jinja2::ValuesMap& addAttrs) {
    Y_ASSERT(attrs);
    auto& map = attrs->GetWritableMap();
    NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ArcadiaRoot, ArcadiaRoot);
    if (ExportFileManager) {
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ExportRoot, ExportFileManager->GetExportRoot());
    }
    if (!addAttrs.empty()) {
        NYexport::MergeTree(map, addAttrs);
    }
    if (DebugOpts_.DebugAttrs) {
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::DumpAttrs, ::NYexport::Dump(attrs->GetMap()), false);
    }
}

void TSpecBasedGenerator::SetSpec(const TGeneratorSpec& spec, const std::string& generatorFile) {
    GeneratorSpec = spec;
    if (GeneratorSpec.Root.Templates.empty() && !generatorFile.empty()) {
        throw TBadGeneratorSpec("[error] No root templates exists in the generator file: " + generatorFile);
    }
    if (GeneratorSpec.Targets.empty()) {
        std::string message = "[error] No targets exists in the generator file: ";
        throw TBadGeneratorSpec(message.append(generatorFile));
    }
    RootTemplates = LoadJinjaTemplates(GeneratorSpec.Root.Templates);
    for (const auto& [targetName, target]: GeneratorSpec.Targets) {
        TargetTemplates[targetName] = LoadJinjaTemplates(target.Templates);
        if (!target.MergePlatformTemplates.empty()) {
            MergePlatformTargetTemplates[targetName] = LoadJinjaTemplates(target.MergePlatformTemplates);
        }
    }
};

}
