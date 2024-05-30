#include "spec_based_generator.h"
#include "yexport_spec.h"
#include "internal_attributes.h"

#include <spdlog/spdlog.h>

#include <regex>

namespace {

    class TPrefixedFileSystem: public jinja2::IFilesystemHandler {
    public:
        TPrefixedFileSystem(const std::string& prefix)
            : Prefix_(prefix)
        {}

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

void TSpecBasedGenerator::OnAttribute(const std::string& attrName, const std::span<const std::string>& attrValue) {
    TAttr attr{attrName};
    for (size_t i = 0; i < attr.Size(); ++i) {
        std::string attrName(attr.GetFirstParts(i));
        UsedAttributes.emplace(attrName);
        auto attrRules = GeneratorSpec.GetAttrRules(attrName);
        if (!attrRules.empty()) {
            UsedRules.insert(attrRules.begin(), attrRules.end());
        }
    }
    auto attrWithValueRules = GeneratorSpec.GetAttrWithValueRules(attrName, attrValue);
    if (!attrWithValueRules.empty()) {
        UsedRules.insert(attrWithValueRules.begin(), attrWithValueRules.end());
    }
}

void TSpecBasedGenerator::OnPlatform(const std::string& platformName) {
    auto rules = GeneratorSpec.GetPlatformRules(platformName);
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

    SetupHandmadeFunctions(JinjaEnv);
}

void TSpecBasedGenerator::SetupHandmadeFunctions(TJinjaEnvPtr& JinjaEnv) {
    auto onHandmadeException = [](const std::string& function, const jinja2::ValuesMap& args, const std::exception& e) {
        spdlog::error("Handmade function '{}({})' exception: '{}', return empty result", function, NYexport::Dump(args), e.what());
    };

    // Handmade split function
    auto splitFunction = [](const std::string& str, const std::string& delimeter, int count) {
        jinja2::ValuesList list;
        if (!str.empty()) {
            size_t bpos = 0;
            size_t dpos;
            while (((dpos = str.find(delimeter, bpos)) != std::string::npos) && (!count || count > 1)) {
                list.emplace_back(str.substr(bpos, dpos - bpos));
                bpos = dpos + delimeter.size();
                if (count > 0) {
                    --count;
                }
            }
            list.emplace_back(str.substr(bpos));
        }
        return list;
    };

    auto rsplitFunction = [](const std::string& str, const std::string& delimeter, int count) {
        jinja2::ValuesList list;
        if (!str.empty()) {
            size_t bpos = str.size() - 1;
            size_t dpos;
            while (((dpos = str.rfind(delimeter, bpos)) != std::string::npos) && (!count || count > 1)) {
                list.insert(list.begin(), str.substr(dpos + 1, bpos - dpos));
                bpos = dpos - delimeter.size();
                if (count > 0) {
                    --count;
                }
            }
            list.insert(list.begin(), str.substr(0, bpos + 1));
        }
        return list;
    };

    static const std::string STR = "str";
    static const std::string DELIMETER = "delimeter";
    static const std::string COUNT = "count";
    static const std::vector<jinja2::ArgInfo>& splitArgs = {
        jinja2::ArgInfo{STR},
        jinja2::ArgInfo{DELIMETER, false, " "},
        jinja2::ArgInfo{COUNT, false, 0/*all items*/},
    };

    auto splitBody = [&](const std::string& function, const jinja2::UserCallableParams& params, std::function<jinja2::ValuesList(const std::string& str, const std::string& delimeter, int count)> splitFunc) {
        try {
            const auto paramStr = params[STR];
            std::string str;
            if (paramStr.isString()) {
                str = paramStr.asString();
            } else if (std::holds_alternative<std::string_view>(paramStr.data())) {
                str = paramStr.get<std::string_view>();
            } else {
                throw std::runtime_error(STR + " is not a string");
            }
            const auto paramDelimeter = params[DELIMETER];
            std::string delimeter;
            if (paramDelimeter.isString()) {
                delimeter = paramDelimeter.asString();
            } else if (std::holds_alternative<std::string_view>(paramDelimeter.data())) {
                delimeter = paramDelimeter.get<std::string_view>();
            } else {
                throw std::runtime_error(DELIMETER + " is not a string");
            }
            const auto paramCount = params[COUNT];
            if (!std::holds_alternative<int64_t>(paramCount.data())) {
                throw std::runtime_error(COUNT + " is not an integer");
            }
            int count = paramCount.get<int64_t>();
            return splitFunc(str, delimeter, count);
        } catch (const std::exception& e) {
            onHandmadeException(function, params.args,  e);
            return jinja2::ValuesList{};
        }
    };

    static const std::string SPLIT = "split";
    JinjaEnv->AddGlobal(SPLIT, jinja2::UserCallable{
        /*fptr=*/[&](const jinja2::UserCallableParams& params) -> jinja2::Value {
            return splitBody(SPLIT, params, splitFunction);
        },
        /*argsInfos=*/ splitArgs
    });
    static const std::string RSPLIT = "rsplit";
    JinjaEnv->AddGlobal(RSPLIT, jinja2::UserCallable{
        /*fptr=*/[&](const jinja2::UserCallableParams& params) -> jinja2::Value {
            return splitBody(RSPLIT, params, rsplitFunction);
        },
        /*argsInfos=*/ splitArgs
    });

    // Handmade map keys function (for iterate by map)
    static const std::string KEYS = "keys";
    static const std::string MAP = "map";
    JinjaEnv->AddGlobal(KEYS, jinja2::UserCallable{
        /*fptr=*/[&](const jinja2::UserCallableParams& params) -> jinja2::Value {\
            try {
                jinja2::ValuesList list;
                const auto map = params[MAP];
                Y_ASSERT(map.isMap());
                const auto* genMap = map.getPtr<jinja2::GenericMap>();
                if (genMap) {// workaround for GenericMap, asMap generate bad_variant_access for it
                    auto keys = genMap->GetKeys();
                    list.insert(list.end(), keys.begin(), keys.end());
                } else {
                    for (const auto& [key, _] : map.asMap()) {
                        list.emplace_back(key);
                    }
                }
                return list;
            } catch (const std::exception& e) {
                onHandmadeException(KEYS, params.args, e);
                return jinja2::ValuesList{};
            }
        },
        /*argsInfos=*/ { jinja2::ArgInfo{MAP} }
    });

    // Handmade dump any variable function for debug jinja2 code
    static const std::string DUMP = "dump";
    static const std::string VAR = "var";
    JinjaEnv->AddGlobal("dump", jinja2::UserCallable{
        /*fptr=*/[&](const jinja2::UserCallableParams& params) -> jinja2::Value {
            try {
                return jinja2::Value{NYexport::Dump(params[VAR])};
            } catch (const std::exception& e) {
                onHandmadeException(DUMP, params.args, e);
                return jinja2::Value{""};
            }
        },
        /*argsInfos=*/ { jinja2::ArgInfo{VAR} }
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

TAttrsPtr TSpecBasedGenerator::MakeAttrs(EAttrGroup eattrGroup, const std::string& name, const TAttrs::TReplacer* toolGetter) const {
    const auto attrGroupIt = GeneratorSpec.AttrGroups.find(eattrGroup);
    if (attrGroupIt != GeneratorSpec.AttrGroups.end()) {
        return TAttrs::Create(attrGroupIt->second, name, Replacer_, toolGetter);
    } else {
        static const TAttrGroup EMPTY_ATTR_GROUP;
        spdlog::error("No attribute specification for {}", ToString<EAttrGroup>(eattrGroup));
        return TAttrs::Create(EMPTY_ATTR_GROUP, name, Replacer_);
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

void TSpecBasedGenerator::CommonFinalizeAttrs(TAttrsPtr& attrs, const jinja2::ValuesMap& addAttrs, bool doDebug) {
    Y_ASSERT(attrs);
    auto& map = attrs->GetWritableMap();
    NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ArcadiaRoot, ArcadiaRoot);
    if (ExportFileManager) {
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ExportRoot, ExportFileManager->GetExportRoot());
    }
    if (!addAttrs.empty()) {
        NYexport::MergeTree(map, addAttrs);
    }
    if (doDebug && DebugOpts_.DebugAttrs) {
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::DumpAttrs, ::NYexport::Dump(attrs->GetMap()));
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

void TSpecBasedGenerator::InitReplacer() {
    if (!GeneratorSpec.SourceRootReplacer.empty() || !GeneratorSpec.BinaryRootReplacer.empty()) {
        static TAttrs::TReplacer REPLACER([this](const std::string& s) -> const std::string& {
            return RootReplacer(s);
        });
        Replacer_ = &REPLACER;
    }
}

const std::string& TSpecBasedGenerator::RootReplacer(const std::string& s) const {
    if ((s.size() < 2) || (GeneratorSpec.SourceRootReplacer.empty() && GeneratorSpec.BinaryRootReplacer.empty())) {
        return s;
    }
    ReplacerBuffer_ = s;
    if (!GeneratorSpec.SourceRootReplacer.empty()) {
        static const std::regex SOURCE_ROOT_RE("(^|\\W)\\$S($|\\W)");
        static const std::string SOURCE_ROOT_REPLACE("$1" + GeneratorSpec.SourceRootReplacer + "$2");
        ReplacerBuffer_ = std::regex_replace(ReplacerBuffer_, SOURCE_ROOT_RE, SOURCE_ROOT_REPLACE);
    }
    if (!GeneratorSpec.BinaryRootReplacer.empty()) {
        static const std::regex BINARY_ROOT_RE("(^|\\W)\\$B($|\\W)");
        static const std::string BINARY_ROOT_REPLACE("$1" + GeneratorSpec.BinaryRootReplacer + "$2");
        ReplacerBuffer_ = std::regex_replace(ReplacerBuffer_, BINARY_ROOT_RE, BINARY_ROOT_REPLACE);
    }
    return ReplacerBuffer_;
}

}
