#include "spec_based_generator.h"
#include "yexport_spec.h"
#include "internal_attributes.h"

#include <contrib/libs/jinja2cpp/include/jinja2cpp/generic_list_iterator.h>

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
                if (count > 1) {
                    if (--count == 1) break;
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
                list.insert(list.begin(), str.substr(dpos + delimeter.size(), bpos - dpos));
                bpos = dpos - delimeter.size();
                if (count > 1) {
                    if (--count == 1) break;
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

    // Handmade exts function
    static const std::string STRING_LIST = "string_list";
    static const std::string ENDS = "ends";
    auto filterByEnds = [](const jinja2::UserCallableParams& params, bool select) {
        jinja2::ValuesList filteredList;
        const auto ends = params[ENDS];
        std::vector<std::string> svEnds;
        if (ends.isList()) {
            auto onEnd = [&svEnds](const jinja2::Value& end) {
                if (end.isString()) {
                    svEnds.emplace_back(std::string_view{end.asString()});
                } else if (std::holds_alternative<std::string_view>(end.data())) {
                    svEnds.emplace_back(end.get<std::string_view>());
                } else {
                    throw std::runtime_error(ENDS + " item is not a string");
                }
            };
            const auto* endsGenList = ends.getPtr<jinja2::GenericList>();
            if (endsGenList) {
                for (const auto& end: *endsGenList) {
                    onEnd(end);
                }
            } else {
                for (const auto& end: ends.asList()) {
                    onEnd(end);
                }
            }
        } else if (ends.isString()) {
            svEnds.emplace_back(ends.asString());
        } else if (std::holds_alternative<std::string_view>(ends.data())) {
            svEnds.emplace_back(ends.get<std::string_view>());
        } else {
            throw std::runtime_error(ENDS + " is not list and not string");
        }
        const auto list = params[STRING_LIST];
        if (!list.isList()) {
            throw std::runtime_error(STRING_LIST + " is not list");
        }
        auto onItem = [&filteredList, &svEnds, &select](const jinja2::Value& item) {
            std::string_view svItem;
            if (item.isString()) {
                svItem = item.asString();
            } else if (std::holds_alternative<std::string_view>(item.data())) {
                svItem = item.get<std::string_view>();
            } else {
                throw std::runtime_error(STRING_LIST + " item is not a string");
            }
            bool foundEnd = false;
            for (const auto& svEnd: svEnds) {
                if (svItem.ends_with(svEnd)) {
                    foundEnd = true;
                    break;
                }
            }
            if ((select && foundEnd) || (!select and !foundEnd)) {
                filteredList.emplace_back(item);
            }
        };
        const auto* genList = list.getPtr<jinja2::GenericList>();
        if (genList) {// workaround for GenericMap, asMap generate bad_variant_access for it
            for (const auto& item: *genList) {
                onItem(item);
            }
        } else {
            for (const auto& item : list.asList()) {
                onItem(item);
            }
        }
        return filteredList;
    };

    // Handmade string list filter function by exts
    static const std::string SELECT_BY_ENDS = "select_by_ends";
    static const std::string REJECT_BY_ENDS = "reject_by_ends";
    JinjaEnv->AddGlobal(SELECT_BY_ENDS, jinja2::UserCallable{
        /*fptr=*/[&](const jinja2::UserCallableParams& params) -> jinja2::Value {
            try {
                return filterByEnds(params, true);
            } catch (const std::exception& e) {
                onHandmadeException(SELECT_BY_ENDS, params.args, e);
                return jinja2::Value{""};
            }
        },
        /*argsInfos=*/ {
            jinja2::ArgInfo{STRING_LIST},
            jinja2::ArgInfo{ENDS},
        }
    });
    JinjaEnv->AddGlobal(REJECT_BY_ENDS, jinja2::UserCallable{
        /*fptr=*/[&](const jinja2::UserCallableParams& params) -> jinja2::Value {
            try {
                return filterByEnds(params, false);
            } catch (const std::exception& e) {
                onHandmadeException(REJECT_BY_ENDS, params.args, e);
                return jinja2::Value{""};
            }
        },
        /*argsInfos=*/ {
            jinja2::ArgInfo{STRING_LIST},
            jinja2::ArgInfo{ENDS},
        }
    });

    // Handmade dump any variable function for debug jinja2 code
    static const std::string DUMP = "dump";
    static const std::string VAR = "var";
    JinjaEnv->AddGlobal(DUMP, jinja2::UserCallable{
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
        auto yexportToml = yexportTomlPath(configDir);
        if (fs::exists(yexportToml)) {
            LoadTargetReplacements(yexportToml, TargetReplacements_);
            return ::NYexport::ReadYexportSpec(yexportToml);
        }
    }
    return {};
}

TAttrsPtr TSpecBasedGenerator::MakeAttrs(EAttrGroup eattrGroup, const std::string& name, const TAttrs::TReplacer* toolGetter, bool listObjectIndexing) const {
    const auto attrGroupIt = GeneratorSpec.AttrGroups.find(eattrGroup);
    if (attrGroupIt != GeneratorSpec.AttrGroups.end()) {
        return TAttrs::Create(attrGroupIt->second, name, Replacer_, toolGetter, listObjectIndexing);
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
            ExportFileManager_->Copy(dir / file, file);
        }
    }
}

std::vector<TJinjaTemplate> TSpecBasedGenerator::LoadJinjaTemplates(const std::vector<TTemplateSpec>& templateSpecs) const {
    return NYexport::LoadJinjaTemplates(GetGeneratorDir(), GetJinjaEnv(), templateSpecs);
}

void TSpecBasedGenerator::RenderJinjaTemplates(TAttrsPtr attrs, std::vector<TJinjaTemplate>& jinjaTemplates, const fs::path& relativeToExportRootDirname, const std::string& platformName) {
    for (auto& jinjaTemplate: jinjaTemplates) {
        jinjaTemplate.SetValueMap(attrs);
        jinjaTemplate.RenderTo(*ExportFileManager_, relativeToExportRootDirname, platformName);
    }
}

void TSpecBasedGenerator::MergePlatforms() {
    THashSet<const TProjectSubdir*> dirs;
    THashMap<fs::path, std::vector<TPlatformPtr>> dir2platforms;// list of platforms for each dir
    for (const auto& platform : Platforms) {
        for (const auto& dir : platform->Project->GetSubdirs()) {
            const auto& macroForTemplate = GetMacroForTemplate(*dir);
            if (!MergePlatformTargetTemplates.contains(macroForTemplate)) {
                continue; // ignore directories without merge platform templates
            }
            dir2platforms[dir->Path].emplace_back(platform);// collect platforms for every dir
            dirs.insert(dir.Get());// and all directories for merge platforms
        }
    }
    for (const auto* dir : dirs) {
        const auto& path = dir->Path;
        const auto& macroForTemplate = GetMacroForTemplate(*dir);
        const auto& dirTemplates = TargetTemplates.at(macroForTemplate);
        auto& mergePlatformTemplates = MergePlatformTargetTemplates.at(macroForTemplate);
        Y_ASSERT(dirTemplates.size() == mergePlatformTemplates.size());
        const auto& dirPlatforms = dir2platforms[path];
        auto templatesCount = dirTemplates.size();
        for (size_t i = 0; i < templatesCount; ++i) {
            const auto& dirTemplate = dirTemplates[i];
            auto& commonTemplate = mergePlatformTemplates[i];
            bool isDifferent = false;
            if (dirPlatforms.size() > 1) {
                TString md5 = ExportFileManager_->MD5(dirTemplate.RenderFilename(path, dirPlatforms[0]->Name));
                for (size_t j = 1; j < dirPlatforms.size(); ++j) {
                    TString otherMd5 = ExportFileManager_->MD5(dirTemplate.RenderFilename(path, dirPlatforms[j]->Name));
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
                commonTemplate.RenderTo(*ExportFileManager_, path);
            } else {
                auto finalPath = commonTemplate.RenderFilename(path, "");
                ExportFileManager_->CopyFromExportRoot(dirTemplate.RenderFilename(path, dirPlatforms[0]->Name), finalPath);
                for (const auto& dirPlatform : dirPlatforms) {
                    ExportFileManager_->Remove(dirTemplate.RenderFilename(path, dirPlatform->Name));
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
        platformAttrs.emplace(platform->Name, platform->Project->Attrs->GetMap());
    }
    NInternalAttrs::EmplaceAttr(attrs->GetWritableMap(), NInternalAttrs::PlatformAttrs, std::move(platformAttrs));
}

void TSpecBasedGenerator::CommonFinalizeAttrs(TAttrsPtr& attrs, const jinja2::ValuesMap& addAttrs, bool doDebug) {
    Y_ASSERT(attrs);
    auto& map = attrs->GetWritableMap();
    NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ArcadiaRoot, ArcadiaRoot);
    if (ExportFileManager_) {
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ExportRoot, ExportFileManager_->GetExportRoot());
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::ProjectRoot, ExportFileManager_->GetProjectRoot());
    }
    if (!addAttrs.empty()) {
        NYexport::MergeTree(map, addAttrs);
    }
    if (doDebug && DebugOpts_.DebugAttrs) {
        std::string dump;
        const auto& attrsMap = attrs->GetMap();
        if (DebugOpts_.DebugSems && attrsMap.contains(NInternalAttrs::DumpSems)) {
            auto tempMap = attrsMap;
            tempMap.erase(NInternalAttrs::DumpSems); // cut off dump_sems attribute from attributes dump
            dump = ::NYexport::Dump(tempMap);
        } else {
            dump = ::NYexport::Dump(attrsMap);
        }
        NInternalAttrs::EmplaceAttr(map, NInternalAttrs::DumpAttrs, dump);
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

void TSpecBasedGenerator::Copy(const fs::path& srcRelPath, const fs::path& dstRelPath) {
    auto srcFullPath = ArcadiaRoot / srcRelPath;
    if (ExportFileManager_) {
        ExportFileManager_->Copy(srcFullPath, dstRelPath);
    } else {
        Copies_.emplace_back(std::make_pair(srcFullPath, dstRelPath));
    }
}

void TSpecBasedGenerator::InitReplacer() {
    if (!GeneratorSpec.SourceRootReplacer.empty() || !GeneratorSpec.BinaryRootReplacer.empty()) {
        static TAttrs::TReplacer REPLACER([this](const std::string& s) -> const std::string& {
            return RootReplacer(s);
        });
        Replacer_ = &REPLACER;
    }
}

const std::string TSpecBasedGenerator::EMPTY_TARGET = "EMPTY";///< Magic target for use in directory without any targets
const std::string TSpecBasedGenerator::EXTRA_ONLY_TARGET ="EXTRA_ONLY";///< Magic target for use in directory with only extra targets without main target

const std::string& TSpecBasedGenerator::GetMacroForTemplate(const NYexport::TProjectSubdir& dir) {
    if (!dir.MainTargetMacro.empty()) {
        return dir.MainTargetMacro;
    }
    if (dir.Targets.empty()) {
        return EMPTY_TARGET;
    } else {
        return EXTRA_ONLY_TARGET;
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

fs::path yexportTomlPath(fs::path configDir) {
    return TSpecBasedGenerator::yexportTomlPath(configDir);
}

}
