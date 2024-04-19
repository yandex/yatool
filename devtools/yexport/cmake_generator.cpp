#include "cmake_generator.h"
#include "generator_spec.h"
#include "std_helpers.h"
#include "read_sem_graph.h"
#include "render_cmake.h"
#include "generator_spec.h"

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/scope.h>
#include <util/generic/vector.h>
#include <util/system/fs.h>
#include <util/system/file.h>

#include <spdlog/spdlog.h>

namespace NYexport {

TProjectConf::TProjectConf(std::string_view name, const fs::path& arcadiaRoot, ECleanIgnored cleanIgnored)
    : ProjectName(name)
    , ArcadiaRoot(arcadiaRoot)
    , CleanIgnored(cleanIgnored)
{
}

TPlatformConf::TPlatformConf(std::string_view platformName)
    : Name(platformName)
{
    CMakeListsFile = fmt::format("CMakeLists.{}.txt", platformName);
}

TPlatform::TPlatform(std::string_view platformName)
    : Conf(platformName)
    , Graph(nullptr)
{
}

TCMakeGenerator::TCMakeGenerator(std::string_view name, const fs::path& arcadiaRoot)
    : Conf(name, arcadiaRoot)
{
    // TODO(YMAKE-91) Use info exported from ymake
}

void TCMakeGenerator::SetArcadiaRoot(const fs::path& arcadiaRoot) {
    ArcadiaRoot = arcadiaRoot;
    Conf.ArcadiaRoot = arcadiaRoot;
}

THolder<TCMakeGenerator> TCMakeGenerator::Load(const fs::path& arcadiaRoot, const std::string& generator, const fs::path& configDir) {
    const auto generatorDir = arcadiaRoot / GENERATORS_ROOT / generator;
    const auto generatorFile = generatorDir / GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        throw yexception() << fmt::format("[error] Failed to load generator {}. No {} file found", generator, generatorFile.c_str());
    }

    THolder<TCMakeGenerator> result = MakeHolder<TCMakeGenerator>();
    result->GeneratorSpec = ReadGeneratorSpec(generatorFile);
    if (result->GeneratorSpec.Common.Templates.size() != 1) {
        throw yexception() << fmt::format("[error] Strong one common template required for generator {}, but not found in {}", generator, generatorFile.c_str());
    }
    if (result->GeneratorSpec.Dir.Templates.empty()) {
        throw yexception() << fmt::format("[error] At least one directory template required for generator {}, but not found in {}", generator, generatorFile.c_str());
    }

    result->GeneratorDir = generatorDir;
    result->SetArcadiaRoot(arcadiaRoot);
    result->SetupJinjaEnv();

    result->YexportSpec = result->ReadYexportSpec(configDir);
    return result;
}

void TCMakeGenerator::SetProjectName(const std::string& projectName) {
    Conf.ProjectName = projectName;
}

void TCMakeGenerator::LoadSemGraph(const std::string& platform, const fs::path& semGraph) {
    if (!platform.empty()) {
        YEXPORT_VERIFY(GeneratorSpec.Platforms.find(platform) != GeneratorSpec.Platforms.end(), fmt::format("No specification for platform \"{}\"", platform));
    }

    auto [graph, startDirs] = ReadSemGraph(semGraph, GeneratorSpec.UseManagedPeersClosure);
    Platforms.emplace_back(MakeSimpleShared<TPlatform>(platform));
    graph.Swap(Platforms.back()->Graph);
    Platforms.back()->StartDirs = std::move(startDirs);
    OnPlatform(platform);
}

void TCMakeGenerator::RenderPlatform(TPlatformPtr platform) {
    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.CollectDirs(*platform->Graph, platform->StartDirs);
    }
    if (!AnalizePlatformSemGraph(Conf, platform, GlobalProperties, this)) {
        yexception() << fmt::format("ERROR: There are exceptions during rendering of platform {}.\n", platform->Conf.Name);
    }

    const auto& generatorSpec = GetGeneratorSpec();
    const auto attrSpecIt = generatorSpec.AttrGroups.find(EAttributeGroup::Directory);
    YEXPORT_VERIFY(attrSpecIt != generatorSpec.AttrGroups.end(), "No attribute specification for dir");

    TTargetAttributesPtr rootdirValuesMap = TTargetAttributes::Create(attrSpecIt->second, "dir");

    auto dirJinjaTemplates = LoadJinjaTemplates(generatorSpec.Dir.Templates);

    auto topLevelSubdirs = jinja2::ValuesList();
    for (auto subdir: platform->Project->GetSubdirs()) {
        Y_ASSERT(subdir);
        platform->SubDirs.insert(subdir->Path);
        if (subdir->IsTopLevel()) {
            topLevelSubdirs.emplace_back(subdir->Path.c_str());
        }
        auto subdirValuesMap = GetSubdirValuesMap(platform, subdir, this);
        RenderJinjaTemplates(subdirValuesMap, dirJinjaTemplates, subdir->Path, platform->Conf.Name);
    }

    const auto [_, inserted] = rootdirValuesMap->GetWritableMap().emplace("subdirs", topLevelSubdirs);
    Y_ASSERT(inserted);

    RenderJinjaTemplates(rootdirValuesMap, dirJinjaTemplates, "", platform->Conf.Name);
}

/// Get dump of semantics tree with values for testing or debug
void TCMakeGenerator::DumpSems(IOutputStream&) {
    spdlog::error("Dump semantics tree of Cmake generator now yet supported");
}

/// Get dump of attributes tree with values for testing or debug
void TCMakeGenerator::DumpAttrs(IOutputStream&) {
    spdlog::error("Dump attributes tree of Cmake generator now yet supported");
}

void TCMakeGenerator::Render(ECleanIgnored cleanIgnored) {
    Conf.CleanIgnored = cleanIgnored;
    RenderPlatforms();
    RenderRoot();
    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.Clean(*ExportFileManager);
    }
}

void TCMakeGenerator::RenderPlatforms() {
    for (auto& platform : Platforms) {
        RenderPlatform(platform);
    }

    CopyFilesAndResources();

    MergePlatforms();
    CopyArcadiaScripts();
}

void TCMakeGenerator::RenderRoot() {
    auto attrSpecIt = GeneratorSpec.AttrGroups.find(EAttributeGroup::Root);
    YEXPORT_VERIFY(attrSpecIt != GeneratorSpec.AttrGroups.end(), "No attribute specification for root");

    auto rootJinjaTemplates = LoadJinjaTemplates(GetGeneratorSpec().Root.Templates);

    TTargetAttributesPtr rootValuesMap = TTargetAttributes::Create(attrSpecIt->second, "root");
    PrepareRootCMakeList(rootValuesMap);
    PrepareConanRequirements(rootValuesMap);
    ApplyRules(*rootValuesMap);
    SetCurrentDirectory(ExportFileManager->GetExportRoot());
    RenderJinjaTemplates(rootValuesMap, rootJinjaTemplates);
}

void TCMakeGenerator::InsertPlatforms(jinja2::ValuesMap& valuesMap, const TVector<TPlatformPtr> platforms) const {
    auto& platformCmakes = valuesMap.insert_or_assign("platform_cmakelists", jinja2::ValuesList()).first->second.asList();
    auto& platformNames = valuesMap.insert_or_assign("platform_names", jinja2::ValuesList()).first->second.asList();
    for (const auto& platform : platforms) {
        platformCmakes.emplace_back(platform->Conf.CMakeListsFile);
        platformNames.emplace_back(platform->Conf.Name);
    }
}

void TCMakeGenerator::MergePlatforms() const {
    TJinjaTemplate commonTemplate;
    const auto& commonTemplateSpec = *GeneratorSpec.Common.Templates.begin();
    auto loaded = commonTemplate.Load(GeneratorDir / commonTemplateSpec.Template, GetJinjaEnv(), commonTemplateSpec.ResultName);
    YEXPORT_VERIFY(loaded, fmt::format("Cannot load template: \"{}\"\n", commonTemplateSpec.Template.c_str()));

    auto attrSpecIt = GeneratorSpec.AttrGroups.find(EAttributeGroup::Directory);
    YEXPORT_VERIFY(attrSpecIt != GeneratorSpec.AttrGroups.end(), "No attribute specification for directory");

    TTargetAttributesPtr dirValueMap = TTargetAttributes::Create(attrSpecIt->second, "dir");
    commonTemplate.SetValueMap(dirValueMap);

    auto& dirMap = dirValueMap->GetWritableMap();
    dirMap["platforms"] = GeneratorSpec.Platforms;

    THashSet<fs::path> visitedDirs;
    for (const auto& platform : Platforms) {
        for (const auto& dir : platform->SubDirs) {
            if (visitedDirs.contains(dir)) {
                continue;
            }

            bool isDifferent = false;
            TString md5 = ExportFileManager->MD5(dir / platform->Conf.CMakeListsFile);
            TVector<TPlatformPtr> dirPlatforms;
            dirPlatforms.emplace_back(platform);
            for (const auto& otherPlatform : Platforms) {
                if (platform->Conf.Name == otherPlatform->Conf.Name) {
                    continue;
                }

                if (otherPlatform->SubDirs.contains(dir)) {
                    if (md5 != ExportFileManager->MD5(dir / otherPlatform->Conf.CMakeListsFile)) {
                        isDifferent = true;
                    }
                    dirPlatforms.emplace_back(otherPlatform);
                } else {
                    isDifferent = true;
                }
            }
            if (isDifferent) {
                InsertPlatforms(dirMap, dirPlatforms);
                commonTemplate.RenderTo(*ExportFileManager, dir, platform->Conf.Name);
            } else {
                auto finalPath = commonTemplate.RenderFilename(dir, platform->Conf.Name);
                ExportFileManager->CopyFromExportRoot(dir / platform->Conf.CMakeListsFile, finalPath);
                for (const auto& dirPlatform : dirPlatforms) {
                    ExportFileManager->Remove(dir / dirPlatform->Conf.CMakeListsFile);
                }
            }
            visitedDirs.insert(dir);
        }
    }
}

TVector<std::string> TCMakeGenerator::GetAdjustedLanguagesList() const {
    /*
        > If enabling ASM, list it last so that CMake can check whether compilers for other languages like C work for assembly too.

        https://cmake.org/cmake/help/latest/command/project.html
    */
    bool hasAsm = false;
    TVector<std::string> languages;

    for (const auto& lang : GlobalProperties.Languages) {
        if (lang == "ASM"sv) {
            hasAsm = true;
        } else {
            languages.emplace_back(lang);
        }
    }
    if (hasAsm) {
        languages.emplace_back("ASM");
    }

    return languages;
}

void TCMakeGenerator::CopyArcadiaScripts() const {
    auto arcadiaScriptDir = Conf.ArcadiaRoot / ArcadiaScriptsRoot;
    for (const auto& script: GlobalProperties.ArcadiaScripts) {
        ExportFileManager->Copy(arcadiaScriptDir / script, CmakeScriptsRoot / script);
    }
}

void TCMakeGenerator::PrepareRootCMakeList(TTargetAttributesPtr rootValueMap) const {
    auto& rootMap = rootValueMap->GetWritableMap();
    rootMap["platforms"] = GeneratorSpec.Platforms;
    InsertPlatforms(rootMap, Platforms);

    jinja2::ValuesList globalVars;
    for (const auto &platform: Platforms) {
        auto varList = jinja2::ValuesList();
        for (const auto &[flag, args]: platform->GlobalVars) {
            auto arg_list = jinja2::ValuesList();
            arg_list.emplace_back(flag);
            arg_list.insert(arg_list.end(), args.begin(), args.end());
            varList.emplace_back(arg_list);
        }
        globalVars.emplace_back(varList);
    }
    rootValueMap->SetAttrValue("platform_vars", globalVars);

    rootValueMap->SetAttrValue("use_conan",
                               !GlobalProperties.ConanPackages.empty() ||
                               !GlobalProperties.ConanToolPackages.empty());
    rootValueMap->SetAttrValue("project_name", Conf.ProjectName);
    rootValueMap->SetAttrValue("project_language_list", GetAdjustedLanguagesList());
    rootValueMap->SetAttrValue("vanilla_protobuf", GlobalProperties.VanillaProtobuf);
}

void TCMakeGenerator::PrepareConanRequirements(TTargetAttributesPtr rootValueMap) const {
    if (GlobalProperties.ConanPackages.empty() && GlobalProperties.ConanToolPackages.empty()) {
        return;
    }

    auto& rootMap = rootValueMap->GetWritableMap();
    rootMap.insert_or_assign("conan_packages",
                             jinja2::ValuesList(GlobalProperties.ConanPackages.begin(),
                                                GlobalProperties.ConanPackages.end()));
    rootMap.insert_or_assign("conan_tool_packages",
                             jinja2::ValuesList(GlobalProperties.ConanToolPackages.begin(),
                                                GlobalProperties.ConanToolPackages.end()));
    rootMap.insert_or_assign("conan_options",
                             jinja2::ValuesList(GlobalProperties.ConanOptions.begin(),
                                                GlobalProperties.ConanOptions.end()));

    auto [conanImportsIt, _] = rootValueMap->GetWritableMap().insert_or_assign(
            "conan_imports",
            jinja2::ValuesList(GlobalProperties.ConanOptions.begin(), GlobalProperties.ConanOptions.end()));
    auto& conanImports = conanImportsIt->second.asList();
    for (std::string_view import: GlobalProperties.ConanImports) {
        // TODO: find better way to avoid unquoting here. Import spec is quoted since
        // I don't know any way of adding semantics arg with spaces in core.conf without
        // quoting it.
        if (import.starts_with('"') && import.ends_with('"')) {
            import.remove_prefix(1);
            import.remove_suffix(1);
        }
        conanImports.emplace_back(import);
    }
}

std::vector<TJinjaTemplate> TCMakeGenerator::LoadJinjaTemplates(const std::vector<TTemplate>& templateSpecs) const {
    return NYexport::LoadJinjaTemplates(GetGeneratorDir(), GetJinjaEnv(), templateSpecs);
}

void TCMakeGenerator::RenderJinjaTemplates(TTargetAttributesPtr valuesMap, std::vector<TJinjaTemplate>& jinjaTemplates, const fs::path& relativeToExportRootDirname, const std::string& platformName) {
    for (auto& jinjaTemplate: jinjaTemplates) {
        jinjaTemplate.SetValueMap(valuesMap);
        jinjaTemplate.RenderTo(*ExportFileManager, relativeToExportRootDirname, platformName);
    }
}

}
