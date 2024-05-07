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
    auto& generatorSpec = result->GeneratorSpec = ReadGeneratorSpec(generatorFile);

    if (generatorSpec.Dir.Templates.empty()) {
        throw yexception() << fmt::format("[error] At least one directory template required for generator {}, but not found in {}", generator, generatorFile.c_str());
    }
    if (generatorSpec.Common.Templates.size() != generatorSpec.Dir.Templates.size()) {
        throw yexception() << fmt::format("[error] Common templates count {} must be equal dir templates count {} for generator {}", generatorSpec.Common.Templates.size(), generatorSpec.Dir.Templates.size(), generator);
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

void TCMakeGenerator::RenderPlatform(TPlatformPtr platform, std::vector<TJinjaTemplate>& dirJinjaTemplates) {
    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.CollectDirs(*platform->Graph, platform->StartDirs);
    }
    if (!AnalizePlatformSemGraph(platform, GlobalProperties, this)) {
        yexception() << fmt::format("ERROR: There are exceptions during rendering of platform {}.\n", platform->Name);
    }

    auto topLevelSubdirs = jinja2::ValuesList();
    for (auto subdir: platform->Project->GetSubdirs()) {
        Y_ASSERT(subdir);
        if (subdir->IsTopLevel()) {
            topLevelSubdirs.emplace_back(subdir->Path.c_str());
        }
        auto subdirValuesMap = GetSubdirValuesMap(platform, subdir, this);
        SetCurrentDirectory(ArcadiaRoot / subdir->Path);
        RenderJinjaTemplates(subdirValuesMap, dirJinjaTemplates, subdir->Path, platform->Name);
    }

    TAttrsPtr rootdirAttrs = MakeAttrs(EAttrGroup::Directory, "rootdir");
    const auto [_, inserted] = rootdirAttrs->GetWritableMap().emplace("subdirs", topLevelSubdirs);
    Y_ASSERT(inserted);
    SetCurrentDirectory(ArcadiaRoot);
    RenderJinjaTemplates(rootdirAttrs, dirJinjaTemplates, "", platform->Name);
}

/// Get dump of semantics tree with values for testing or debug
void TCMakeGenerator::DumpSems(IOutputStream&) const {
    spdlog::error("Dump semantics tree of Cmake generator now yet supported");
}

/// Get dump of attributes tree with values for testing or debug
void TCMakeGenerator::DumpAttrs(IOutputStream&) {
    spdlog::error("Dump attributes tree of Cmake generator now yet supported");
}

bool TCMakeGenerator::IgnorePlatforms() const {
    return false;// always require platforms
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
    auto dirJinjaTemplates = LoadJinjaTemplates(GeneratorSpec.Dir.Templates);
    for (auto& platform : Platforms) {
        RenderPlatform(platform, dirJinjaTemplates);
    }
    CopyFilesAndResources();
    MergePlatforms(dirJinjaTemplates);
    CopyArcadiaScripts();
}

void TCMakeGenerator::RenderRoot() {
    auto rootJinjaTemplates = LoadJinjaTemplates(GetGeneratorSpec().Root.Templates);
    auto rootAttrs = MakeAttrs(EAttrGroup::Root, "root");
    PrepareRootCMakeList(rootAttrs);
    PrepareConanRequirements(rootAttrs);
    ApplyRules(rootAttrs);
    SetCurrentDirectory(ArcadiaRoot);
    RenderJinjaTemplates(rootAttrs, rootJinjaTemplates);
}

void TCMakeGenerator::InsertPlatforms(jinja2::ValuesMap& valuesMap, const TVector<TPlatformPtr> platforms) const {
    auto& platformNames = valuesMap.insert_or_assign("platform_names", jinja2::ValuesList()).first->second.asList();
    for (const auto& platform : platforms) {
        platformNames.emplace_back(platform->Name);
    }
}

void TCMakeGenerator::MergePlatforms(const std::vector<TJinjaTemplate>& dirJinjaTemplates) const {
    auto commonJinjaTemplates = LoadJinjaTemplates(GeneratorSpec.Common.Templates);
    TSpecBasedGenerator::MergePlatforms(dirJinjaTemplates, commonJinjaTemplates);
}

jinja2::ValuesList TCMakeGenerator::GetAdjustedLanguagesList() const {
    /*
        > If enabling ASM, list it last so that CMake can check whether compilers for other languages like C work for assembly too.

        https://cmake.org/cmake/help/latest/command/project.html
    */
    bool hasAsm = false;
    jinja2::ValuesList languages;

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

void TCMakeGenerator::PrepareRootCMakeList(TAttrsPtr rootValueMap) const {
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
    rootMap["platform_vars"] = globalVars;

    rootMap["use_conan"] = !GlobalProperties.ConanPackages.empty() || !GlobalProperties.ConanToolPackages.empty();
    rootMap["project_name"] = Conf.ProjectName;
    rootMap["project_language_list"] = GetAdjustedLanguagesList();
    rootMap["vanilla_protobuf"] = GlobalProperties.VanillaProtobuf;
}

void TCMakeGenerator::PrepareConanRequirements(TAttrsPtr rootValueMap) const {
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
            jinja2::ValuesList{});
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

}
