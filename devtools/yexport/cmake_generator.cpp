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

TPlatformConf::TPlatformConf(std::string_view platformName, const TGeneratorSpec& generatorSpec)
    : Name(platformName)
{
    if (platformName == "linux" || platformName == "linux-x86_64") {
        Platform = EPlatform::EP_Linux_x86_64;
    } else if (platformName == "linux-x86_64-cuda") {
        Platform = EPlatform::EP_Linux_x86_64_Cuda;
    } else if (platformName == "linux-aarch64" || platformName == "linux-arm64") {
        Platform = EPlatform::EP_Linux_Aarch64;
    } else if (platformName == "linux-aarch64-cuda" || platformName == "linux-arm64-cuda") {
        Platform = EPlatform::EP_Linux_Aarch64_Cuda;
    } else if (platformName == "linux-ppc64le") {
        Platform = EPlatform::EP_Linux_Ppc64LE;
    } else if (platformName == "linux-ppc64le-cuda") {
        Platform = EPlatform::EP_Linux_Ppc64LE_Cuda;
    } else if (platformName == "darwin" || platformName == "darwin-x86_64") {
        Platform = EPlatform::EP_MacOs_x86_64;
    } else if (platformName == "darwin-arm64") {
        Platform = EPlatform::EP_MacOs_Arm64;
    } else if (platformName == "windows" || platformName == "windows-x86_64") {
        Platform = EPlatform::EP_Windows_x86_64;
    } else if (platformName == "windows-x86_64-cuda") {
        Platform = EPlatform::EP_Windows_x86_64_Cuda;
    } else if (platformName == "android-arm" || platformName == "android-arm32") {
        Platform = EPlatform::EP_Android_Arm;
    } else if (platformName == "android-arm64") {
        Platform = EPlatform::EP_Android_Arm64;
    } else if (platformName == "android-x86") {
        Platform = EPlatform::EP_Android_x86;
    } else if (platformName == "android-x86_64") {
        Platform = EPlatform::EP_Android_x86_64;
    } else {
        throw yexception() << "Unsupported platform " << platformName;
    }
    const auto& flagIt = generatorSpec.Platforms.find(platformName.data());
    CMakeFlag = (flagIt != generatorSpec.Platforms.end()) ? flagIt->second.asString() : "";
    CMakeListsFile = fmt::format("CMakeLists.{}.txt", platformName);
}

TPlatform::TPlatform(std::string_view platformName, const TGeneratorSpec& generatorSpec)
    : Conf(platformName, generatorSpec)
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

void TCMakeGenerator::SaveConanProfile(const std::string_view& profile) const {
    auto path = fs::path("cmake") / "conan-profiles" / profile;
    ExportFileManager->Copy( GeneratorDir / path, path);
}

THolder<TCMakeGenerator> TCMakeGenerator::Load(const fs::path& arcadiaRoot, const std::string& generator, const fs::path& configDir) {
    const auto generatorDir = arcadiaRoot / GENERATORS_ROOT / generator;
    const auto generatorFile = generatorDir / GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        throw yexception() << fmt::format("[error] Failed to load generator {}. No {} file found", generator, generatorFile.c_str());
    }

    THolder<TCMakeGenerator> result = MakeHolder<TCMakeGenerator>();
    result->GeneratorSpec = ReadGeneratorSpec(generatorFile);
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
    const auto& platformIt = GeneratorSpec.Platforms.find(platform);
    YEXPORT_VERIFY(platformIt != GeneratorSpec.Platforms.end(),
                   fmt::format("No specification for platform \"{}\"", platform));

    auto [graph, startDirs] = ReadSemGraph(semGraph, GeneratorSpec.UseManagedPeersClosure);
    Platforms.emplace_back(MakeSimpleShared<TPlatform>(platform, GeneratorSpec));
    graph.Swap(Platforms.back()->Graph);
    Platforms.back()->StartDirs = std::move(startDirs);
    OnPlatform(platform);
}

void TCMakeGenerator::RenderPlatform(const TPlatformPtr platform) {
    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.CollectDirs(*platform->Graph, platform->StartDirs);
    }
    if (!RenderCmake(Conf, platform, GlobalProperties, this)) {
        yexception() << fmt::format("ERROR: There are exceptions during rendering of platform {}.\n", platform->Conf.Name);
    }
}

/// Get dump of attributes tree with values for testing
void TCMakeGenerator::DumpAttrs(IOutputStream&) {
    spdlog::error("Dump attributes tree of Cmake generator now yet supported");
}

void TCMakeGenerator::Render(ECleanIgnored cleanIgnored) {
    Conf.CleanIgnored = cleanIgnored;

    for (auto& platform : Platforms) {
        RenderPlatform(platform);
    }

    CopyFilesAndResources();

    MergePlatforms();
    CopyArcadiaScripts();

    {
        auto attrSpecIt = GeneratorSpec.AttrGroups.find(EAttributeGroup::Root);
        YEXPORT_VERIFY(attrSpecIt != GeneratorSpec.AttrGroups.end(),
                       "No attribute specification for root");

        TTargetAttributesPtr rootValueMap = TTargetAttributes::Create(attrSpecIt->second, "root");

        PrepareRootCMakeList(rootValueMap);
        PrepareConanRequirements(rootValueMap);
        ApplyRules(*rootValueMap);
        RenderRootTemplates(rootValueMap);
    }

    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.Clean(*ExportFileManager);
    }
}

void TCMakeGenerator::InsertPlatforms(jinja2::ValuesMap& valuesMap, const TVector<TPlatformPtr> platforms) const {
    auto& platformCmakes = valuesMap.insert_or_assign("platform_cmakelists", jinja2::ValuesList()).first->second.asList();
    auto& platformNames = valuesMap.insert_or_assign("platform_names", jinja2::ValuesList()).first->second.asList();
    auto& platformFlags = valuesMap.insert_or_assign("platform_flags", jinja2::ValuesList()).first->second.asList();
    for (const auto& platform : platforms) {
        platformCmakes.emplace_back(platform->Conf.CMakeListsFile);
        platformNames.emplace_back(platform->Conf.Name);
        platformFlags.emplace_back(platform->Conf.CMakeFlag);
    }
}
void TCMakeGenerator::MergePlatforms() const {
    TJinjaTemplate commonTemplate;
    auto loaded = commonTemplate.Load(GeneratorDir / "common_cmake_lists.jinja", GetJinjaEnv());
    YEXPORT_VERIFY(loaded, fmt::format("Cannot load template: \"{}\"\n", "common_cmake_lists.jinja"));

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
                if (platform->Conf.Platform == otherPlatform->Conf.Platform) {
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
                commonTemplate.RenderTo(*ExportFileManager, dir / NCMake::CMakeListsFile);
            } else {
                auto finalPath = dir / NCMake::CMakeListsFile;
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
    {
        auto& rootMap = rootValueMap->GetWritableMap();
        rootMap["platforms"] = GeneratorSpec.Platforms;
        InsertPlatforms(rootMap, Platforms);
    }
    {
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
    }

    rootValueMap->SetAttrValue("use_conan",
                               !GlobalProperties.ConanPackages.empty() ||
                               !GlobalProperties.ConanToolPackages.empty());
    rootValueMap->SetAttrValue("project_name", Conf.ProjectName);
    rootValueMap->SetAttrValue("project_language_list", GetAdjustedLanguagesList());
}

void TCMakeGenerator::PrepareConanRequirements(TTargetAttributesPtr rootValueMap) const {
    if (GlobalProperties.ConanPackages.empty() && GlobalProperties.ConanToolPackages.empty()) {
        return;
    }

    for (const auto& platform: Platforms) {
        switch (platform->Conf.Platform) {
            case EPlatform::EP_Linux_x86_64:
            case EPlatform::EP_Linux_x86_64_Cuda:
            case EPlatform::EP_MacOs_x86_64:
            case EPlatform::EP_Windows_x86_64:
            case EPlatform::EP_Windows_x86_64_Cuda:
            case EPlatform::EP_Other:
                break;
            case EPlatform::EP_Android_Arm: SaveConanProfile("android.armv7.profile"); break;
            case EPlatform::EP_Android_Arm64: SaveConanProfile("android.arm64.profile"); break;
            case EPlatform::EP_Android_x86: SaveConanProfile("android.x86.profile"); break;
            case EPlatform::EP_Android_x86_64: SaveConanProfile("android.x86_64.profile"); break;
            case EPlatform::EP_Linux_Aarch64:
            case EPlatform::EP_Linux_Aarch64_Cuda:
                SaveConanProfile("linux.aarch64.profile");
                break;
            case EPlatform::EP_Linux_Ppc64LE:
            case EPlatform::EP_Linux_Ppc64LE_Cuda:
                SaveConanProfile("linux.ppc64le.profile");
                break;
            case EPlatform::EP_MacOs_Arm64:
                SaveConanProfile("macos.arm64.profile");
                break;
        }
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

    {
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
}

void TCMakeGenerator::RenderRootTemplates(TTargetAttributesPtr rootValueMap) const {
    SetCurrentDirectory(ExportFileManager->GetExportRoot());

    const auto& rootTemplates = GeneratorSpec.Root.Templates;
    for (const auto& tmpl : rootTemplates) {
        TJinjaTemplate rootTempate;
        auto loaded = rootTempate.Load(GeneratorDir / tmpl.Template, GetJinjaEnv());
        YEXPORT_VERIFY(loaded,
                       fmt::format("Cannot load template: \"{}\"\n", tmpl.Template.string()));
        rootTempate.SetValueMap(rootValueMap);
        rootTempate.RenderTo(*ExportFileManager, tmpl.ResultName);
    }
}

}
