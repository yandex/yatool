#include "yexport_generator.h"

#include "cmake_generator.h"
#include "jinja_generator.h"
#include "generator_spec.h"
#include "generators.h"
#include "py_requirements_generator.h"
#include "spec_based_generator.h"

#include <util/generic/scope.h>

#include <spdlog/spdlog.h>

namespace NYexport {

THolder<TYexportGenerator> Load(const std::string& generator, const fs::path& arcadiaRoot, const fs::path& configDir) {
    if (generator == NGenerators::HARDCODED_CMAKE_GENERATOR) {
        return TCMakeGenerator::Load(arcadiaRoot, generator, configDir);
    }
    if (generator == NGenerators::HARDCODED_PY3_REQUIREMENTS_GENERATOR) {
        return TPyRequirementsGenerator::Load(arcadiaRoot, EPyVer::Py3);
    }
    if (generator == NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR) {
        return TPyRequirementsGenerator::Load(arcadiaRoot, EPyVer::Py2);
    }
    return TJinjaGenerator::Load(arcadiaRoot, generator, configDir);
}

void TYexportGenerator::RenderTo(const fs::path& exportRoot, ECleanIgnored cleanIgnored) {
    ExportFileManager = MakeHolder<TExportFileManager>(exportRoot);
    // Prevents from exporting the root from the previous render, which failed due to an exception
    Y_DEFER {
        ExportFileManager = nullptr;
    };
    Render(cleanIgnored);
}
TExportFileManager* TYexportGenerator::GetExportFileManager(){
    return ExportFileManager.Get();
}

TVector<std::string> GetAvailableGenerators(const fs::path& arcadiaRoot) {
    TVector<std::string> generators{NGenerators::HARDCODED_CMAKE_GENERATOR, NGenerators::HARDCODED_PY3_REQUIREMENTS_GENERATOR, NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR};
    if (arcadiaRoot.empty()) {
        return generators;
    }
    const fs::path generators_root = arcadiaRoot / TSpecBasedGenerator::GENERATORS_ROOT;
    for (auto const& dir_entry : fs::directory_iterator{generators_root}) {
        if (dir_entry.is_directory() && fs::exists(dir_entry.path() / TSpecBasedGenerator::GENERATOR_FILE)) {
            generators.push_back(dir_entry.path().filename().c_str());
        }
    }
    return generators;
}

}
