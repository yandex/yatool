#include "yexport_generator.h"

#include "jinja_generator.h"
#include "generators.h"
#include "py_requirements_generator.h"

#include <util/generic/scope.h>

#include <spdlog/spdlog.h>

namespace NYexport {

THolder<TYexportGenerator> Load(const std::string& generator, const fs::path& arcadiaRoot, const fs::path& configDir,
    const std::optional<TDumpOpts> dumpOpts, const std::optional<TDebugOpts> debugOpts
) {
    if (generator == NGenerators::HARDCODED_PY3_REQUIREMENTS_GENERATOR) {
        return TPyRequirementsGenerator::Load(arcadiaRoot, EPyVer::Py3);
    } else if (generator == NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR) {
        return TPyRequirementsGenerator::Load(arcadiaRoot, EPyVer::Py2);
    } else {
        return TJinjaGenerator::Load(arcadiaRoot, generator, configDir, dumpOpts, debugOpts);
    }
}

void TYexportGenerator::RenderTo(const fs::path& exportRoot, ECleanIgnored cleanIgnored) {
    ExportFileManager_ = MakeHolder<TExportFileManager>(exportRoot);
    if (!Copies_.empty()) {
        for (const auto& [srcFullPath, dstRelPath] : Copies_) {
            ExportFileManager_->Copy(srcFullPath, dstRelPath);
        }
    }
    // Prevents from exporting the root from the previous render, which failed due to an exception
    Y_DEFER {
        ExportFileManager_ = nullptr;
    };
    Render(cleanIgnored);
}

TExportFileManager* TYexportGenerator::GetExportFileManager(){
    return ExportFileManager_.Get();
}

TVector<std::string> GetAvailableGenerators(const fs::path& arcadiaRoot) {
    TVector<std::string> generators{NGenerators::HARDCODED_PY3_REQUIREMENTS_GENERATOR, NGenerators::HARDCODED_PY2_REQUIREMENTS_GENERATOR};
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
