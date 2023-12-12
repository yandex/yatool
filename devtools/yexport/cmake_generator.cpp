#include "cmake_generator.h"
#include "generator_spec.h"
#include "path_hash.h"
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

namespace {
    void FormatCommonCMakeText(fmt::memory_buffer& buf, const TVector<TPlatformConf>& platforms) {
        auto bufIt = std::back_inserter(buf);
        fmt::format_to(bufIt, "{}", NCMake::GeneratedDisclamer);
        for (auto it = platforms.begin(); it != platforms.end(); it++) {
            if (it == platforms.begin()) {
                fmt::format_to(bufIt, "if ({})\n  include({})\n", it->CMakeFlag, it->CMakeListsFile);
                continue;
            }
            fmt::format_to(bufIt, "elseif ({})\n  include({})\n", it->CMakeFlag, it->CMakeListsFile);
        }
        fmt::format_to(bufIt, "endif()\n");
    }

    void SaveConanProfile(TExportFileManager& exportFileManager, const std::string_view& profile) {
        exportFileManager.CopyResource( fs::path("cmake") / "conan-profiles" / profile);
    }
}

TProjectConf::TProjectConf(std::string_view name, const fs::path& arcadiaRoot, ECleanIgnored cleanIgnored)
    : ProjectName(name)
    , ArcadiaRoot(arcadiaRoot)
    , CleanIgnored(cleanIgnored)
{
}

TPlatformConf::TPlatformConf(std::string_view platformName) {
    if (platformName == "linux" || platformName == "linux-x86_64") {
        Platform = EPlatform::EP_Linux_x86_64;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"x86_64\" AND NOT HAVE_CUDA";
    } else if (platformName == "linux-x86_64-cuda") {
        Platform = EPlatform::EP_Linux_x86_64_Cuda;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"x86_64\" AND HAVE_CUDA";
    } else if (platformName == "linux-aarch64" || platformName == "linux-arm64") {
        Platform = EPlatform::EP_Linux_Aarch64;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"aarch64\" AND NOT HAVE_CUDA";
    } else if (platformName == "linux-aarch64-cuda" || platformName == "linux-arm64-cuda") {
        Platform = EPlatform::EP_Linux_Aarch64_Cuda;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"aarch64\" AND HAVE_CUDA";
    } else if (platformName == "linux-ppc64le") {
        Platform = EPlatform::EP_Linux_Ppc64LE;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"ppc64le\" AND NOT HAVE_CUDA";
    } else if (platformName == "linux-ppc64le-cuda") {
        Platform = EPlatform::EP_Linux_Ppc64LE_Cuda;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Linux\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"ppc64le\" AND HAVE_CUDA";
    } else if (platformName == "darwin" || platformName == "darwin-x86_64") {
        Platform = EPlatform::EP_MacOs_x86_64;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Darwin\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"x86_64\"";
    } else if (platformName == "darwin-arm64") {
        Platform = EPlatform::EP_MacOs_Arm64;
        CMakeFlag = "CMAKE_SYSTEM_NAME STREQUAL \"Darwin\" AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"arm64\"";
    } else if (platformName == "windows" || platformName == "windows-x86_64") {
        Platform = EPlatform::EP_Windows_x86_64;
        CMakeFlag = "WIN32 AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"AMD64\" AND NOT HAVE_CUDA";
    } else if (platformName == "windows-x86_64-cuda") {
        Platform = EPlatform::EP_Windows_x86_64_Cuda;
        CMakeFlag = "WIN32 AND CMAKE_SYSTEM_PROCESSOR STREQUAL \"AMD64\" AND HAVE_CUDA";
    } else if (platformName == "android-arm" || platformName == "android-arm32") {
        Platform = EPlatform::EP_Android_Arm;
        CMakeFlag = "ANDROID AND CMAKE_ANDROID_ARCH STREQUAL \"arm\"";
    } else if (platformName == "android-arm64") {
        Platform = EPlatform::EP_Android_Arm64;
        CMakeFlag = "ANDROID AND CMAKE_ANDROID_ARCH STREQUAL \"arm64\"";
    } else if (platformName == "android-x86") {
        Platform = EPlatform::EP_Android_x86;
        CMakeFlag = "ANDROID AND CMAKE_ANDROID_ARCH STREQUAL \"x86\"";
    } else if (platformName == "android-x86_64") {
        Platform = EPlatform::EP_Android_x86_64;
        CMakeFlag = "ANDROID AND CMAKE_ANDROID_ARCH STREQUAL \"x86_64\"";
    } else {
        throw yexception() << "Unsupported platform " << platformName;
    }
    CMakeListsFile = fmt::format("CMakeLists.{}.txt", platformName);
}

TPlatform::TPlatform(std::string_view platformName)
    : Conf(platformName)
    , Graph(nullptr)
    , Name(platformName)
{
}

TCMakeGenerator::TCMakeGenerator(std::string_view name, const fs::path& arcadiaRoot)
    : Conf(name, arcadiaRoot)
{
    // TODO(YMAKE-91) Use info exported from ymake
}

void TCMakeGenerator::UpdateGlobalModules() {
    GlobalProperties.GlobalModules.emplace("cmake/global_flags.cmake");
}

THolder<TCMakeGenerator> TCMakeGenerator::Load(const fs::path& arcadiaRoot, const std::string& generator, const fs::path& configDir) {
    const auto generatorDir = arcadiaRoot / GENERATORS_ROOT / generator;
    const auto generatorFile = generatorDir / GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        throw yexception() << fmt::format("[error] Failed to load generator {}. No {} file found", generator, generatorFile.c_str());
    }

    THolder<TCMakeGenerator> result = MakeHolder<TCMakeGenerator>();
    result->Conf.ArcadiaRoot = arcadiaRoot;
    result->GeneratorSpec = ReadGeneratorSpec(generatorFile, CopyFilesOnly);
    result->GeneratorDir = generatorDir;

    result->ReadYexportSpec(configDir);
    return result;
}

void TCMakeGenerator::SetProjectName(const std::string& projectName) {
    Conf.ProjectName = projectName;
}

void TCMakeGenerator::LoadSemGraph(const std::string& platform, const fs::path& semGraph) {
        auto [graph, startDirs] = ReadSemGraph(semGraph);
        Platforms.emplace_back(TPlatform(platform));
        graph.Swap(Platforms.back().Graph);
        Platforms.back().StartDirs = std::move(startDirs);
}

void TCMakeGenerator::RenderPlatform(TPlatform& platform) {
    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.CollectDirs(*platform.Graph, platform.StartDirs);
    }
    if (!RenderCmake(platform.StartDirs, *platform.Graph, Conf, platform, GlobalProperties, this)) {
        yexception() << fmt::format("ERROR: There are exceptions during rendering of platform {}.\n", platform.Name);
    }
}

/// Get dump of attributes tree with values for testing
void TCMakeGenerator::Dump(IOutputStream&) {
    spdlog::error("Dump of Cmake generator now yet supported");
}

void TCMakeGenerator::Render(ECleanIgnored cleanIgnored) {
    Conf.CleanIgnored = cleanIgnored;

    UpdateGlobalModules();
    for (auto& platform : Platforms) {
        RenderPlatform(platform);
    }

    CopyFilesAndResources();

    MergePlatforms();
    RenderRootCMakeList();
    CopyArcadiaScripts();
    RenderConanRequirements();

    if (Conf.CleanIgnored == ECleanIgnored::Enabled) {
        Cleaner.Clean(*ExportFileManager);
    }
}

void TCMakeGenerator::MergePlatforms() const {
    THashSet<fs::path> visitedDirs;
    for (const auto& platform : Platforms) {
        for (const auto& dir : platform.SubDirs) {
            if (visitedDirs.contains(dir)) {
                continue;
            }

            bool isDifferent = false;
            TString md5 = ExportFileManager->MD5(dir / platform.Conf.CMakeListsFile);
            TVector<TPlatformConf> dirPlatforms;
            dirPlatforms.push_back(platform.Conf);
            for (const auto& otherPlatform : Platforms) {
                if (platform.Conf.Platform == otherPlatform.Conf.Platform) {
                    continue;
                }

                if (otherPlatform.SubDirs.contains(dir)) {
                    if (md5 != ExportFileManager->MD5(dir / otherPlatform.Conf.CMakeListsFile)) {
                        isDifferent = true;
                    }
                    dirPlatforms.push_back(otherPlatform.Conf);
                } else {
                    isDifferent = true;
                }
            }
            if (isDifferent) {
                fmt::memory_buffer buf;
                FormatCommonCMakeText(buf, dirPlatforms);
                auto out = ExportFileManager->Open(dir / NCMake::CMakeListsFile);
                out.Write(buf.data(), buf.size());
            } else {
                auto finalPath = dir / NCMake::CMakeListsFile;
                ExportFileManager->CopyFromExportRoot(dir / platform.Conf.CMakeListsFile, finalPath);
                for (const auto& platform : dirPlatforms) {
                    ExportFileManager->Remove(dir / platform.CMakeListsFile);
                }
            }
            visitedDirs.insert(dir);
        }
    }
}

bool TCMakeGenerator::SaveGlobalVars(std::string_view modName) const {
    bool hasGlobalVars = false;
    for (const auto& platform : Platforms) {
        if (platform.GlobalVars.empty()) {
            continue;
        }
        hasGlobalVars = true;
        break;
    }

    if (!hasGlobalVars) {
        return false;
    }
    auto out = ExportFileManager->Open(fs::path("cmake") / modName);
    fmt::memory_buffer buf;
    auto bufIt = std::back_inserter(buf);

    fmt::format_to(bufIt, "{}", NCMake::GeneratedDisclamer);

    for (const auto& platform : Platforms) {
        fmt::format_to(bufIt, "if({})\n", platform.Conf.CMakeFlag);
        for(const auto& [flag, args] : platform.GlobalVars) {
            fmt::format_to(bufIt, "  set({} {})\n", flag, fmt::join(args, " "));
        }
        fmt::format_to(bufIt, "endif()\n\n");
    }
    out.Write(buf.data(), buf.size());

    return true;
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
            languages.push_back(lang);
        }
    }
    if (hasAsm) {
        languages.push_back("ASM");
    }

    return languages;
}

void TCMakeGenerator::RenderRootCMakeList() const {
    auto out = ExportFileManager->Open(NCMake::CMakeListsFile);
    fmt::memory_buffer buf;
    auto bufIt = std::back_inserter(buf);

    fmt::format_to(bufIt, "{}", NCMake::GeneratedDisclamer);
    fmt::format_to(bufIt, "cmake_minimum_required(VERSION 3.15)\n");
    fmt::format_to(bufIt, "project({} LANGUAGES", Conf.ProjectName);
    for (const auto& lang : GetAdjustedLanguagesList()) {
        fmt::format_to(bufIt, " {}", lang);
    }
    fmt::format_to(bufIt, ")\n\n");
    fmt::format_to(bufIt, "set(BUILD_SHARED_LIBS Off)\n");
    fmt::format_to(bufIt, "set(CMAKE_CXX_STANDARD 20)\n");
    fmt::format_to(bufIt, "set(CMAKE_CXX_EXTENSIONS Off)\n");
    fmt::format_to(bufIt, "set({}_MAX_LINK_JOBS 5 CACHE STRING \"Maximum parallel link jobs for Ninja generator\")\n", Conf.ProjectName);
    fmt::format_to(bufIt, "set_property(GLOBAL PROPERTY JOB_POOLS link_jobs=${{{}_MAX_LINK_JOBS}})\n", Conf.ProjectName);
    fmt::format_to(bufIt, "set(CMAKE_JOB_POOL_LINK link_jobs)\n");

    fmt::format_to(bufIt, "\nlist(APPEND CMAKE_MODULE_PATH ${{CMAKE_BINARY_DIR}} ${{CMAKE_SOURCE_DIR}}/cmake)\n");
    fmt::format_to(bufIt, "include_directories(${{CMAKE_SOURCE_DIR}} ${{CMAKE_BINARY_DIR}})\n");
    fmt::format_to(bufIt, "list(APPEND CMAKE_CTEST_ARGUMENTS \"--output-on-failure\")\n");
    fmt::format_to(bufIt, "enable_testing()\n");

    fmt::format_to(bufIt, "\n# Disable 'empty CUDA_ARCHITECTURES not allowed' warning\n");
    fmt::format_to(bufIt, "# Can't set it in cuda.cmake because of CMake policy subdirectory stack rules\n");
    fmt::format_to(bufIt, "cmake_policy(SET CMP0104 OLD)\n\n");

    for (const auto& path : GlobalProperties.GlobalModules) {
        fmt::format_to(bufIt, "include({})\n", path.c_str());
    }

    if (SaveGlobalVars("global_vars.cmake")) {
        fmt::format_to(bufIt, "include(cmake/global_vars.cmake)\n");
    }

    if (!GlobalProperties.ConanPackages.empty() || !GlobalProperties.ConanToolPackages.empty()) {
        fmt::format_to(bufIt, "{}", Conf.ConanSetupSection);
    }

    for (auto it = Platforms.begin(); it != Platforms.end(); it++) {
        if (it == Platforms.begin()) {
            fmt::format_to(bufIt, "if ({})\n  include({})\n", it->Conf.CMakeFlag, it->Conf.CMakeListsFile);
            continue;
        }
        fmt::format_to(bufIt, "elseif ({})\n  include({})\n", it->Conf.CMakeFlag, it->Conf.CMakeListsFile);
    }
    fmt::format_to(bufIt, "endif()\n");

    out.Write(buf.data(), buf.size());
}

void TCMakeGenerator::CopyArcadiaScripts() const {
    auto arcadiaScriptDir = Conf.ArcadiaRoot / ArcadiaScriptsRoot;
    for (const auto& script: GlobalProperties.ArcadiaScripts) {
        ExportFileManager->Copy(arcadiaScriptDir / script, CmakeScriptsRoot / script);
    }
}

void TCMakeGenerator::RenderConanRequirements() const {
    if (GlobalProperties.ConanPackages.empty() && GlobalProperties.ConanToolPackages.empty()) {
        return;
    }

    for (const auto& platform: Platforms) {
        switch (platform.Conf.Platform) {
            case EPlatform::EP_Linux_x86_64:
            case EPlatform::EP_Linux_x86_64_Cuda:
            case EPlatform::EP_MacOs_x86_64:
            case EPlatform::EP_Windows_x86_64:
            case EPlatform::EP_Windows_x86_64_Cuda:
            case EPlatform::EP_Other:
                break;
            case EPlatform::EP_Android_Arm: SaveConanProfile(*ExportFileManager, "android.armv7.profile"); break;
            case EPlatform::EP_Android_Arm64: SaveConanProfile(*ExportFileManager, "android.arm64.profile"); break;
            case EPlatform::EP_Android_x86: SaveConanProfile(*ExportFileManager, "android.x86.profile"); break;
            case EPlatform::EP_Android_x86_64: SaveConanProfile(*ExportFileManager, "android.x86_64.profile"); break;
            case EPlatform::EP_Linux_Aarch64:
            case EPlatform::EP_Linux_Aarch64_Cuda:
                SaveConanProfile(*ExportFileManager, "linux.aarch64.profile");
                break;
            case EPlatform::EP_Linux_Ppc64LE:
            case EPlatform::EP_Linux_Ppc64LE_Cuda:
                SaveConanProfile(*ExportFileManager, "linux.ppc64le.profile");
                break;
            case EPlatform::EP_MacOs_Arm64:
                SaveConanProfile(*ExportFileManager, "macos.arm64.profile");
                break;
        }
    }
    auto out = ExportFileManager->Open("conanfile.txt");
    fmt::memory_buffer buf;
    auto bufIt = std::back_inserter(buf);

    fmt::format_to(bufIt, "[requires]\n");
    for (const auto& pkg: GlobalProperties.ConanPackages) {
        fmt::format_to(bufIt, "{}\n", pkg);
    }

    fmt::format_to(bufIt, "\n[tool_requires]\n");
    for (const auto& pkg: GlobalProperties.ConanToolPackages) {
        fmt::format_to(bufIt, "{}\n", pkg);
    }

    fmt::format_to(bufIt, "\n[options]\n");
    for (const auto& opt: GlobalProperties.ConanOptions) {
        fmt::format_to(bufIt, "{}\n", opt);
    }

    fmt::format_to(bufIt, "\n[imports]\n");
    for (std::string_view import: GlobalProperties.ConanImports) {
        // TODO: find better way to avoid unquoting here. Import spec is quoted since
        // I don't know any way of adding semantics arg with spaces in core.conf without
        // quoting it.
        if (import.starts_with('"') && import.ends_with('"')) {
            import.remove_prefix(1);
            import.remove_suffix(1);
        }
        fmt::format_to(bufIt, "{}\n", import);
    }

    fmt::format_to(bufIt, "\n[generators]\ncmake_find_package\ncmake_paths\n");
    out.Write(buf.data(), buf.size());
}

}
