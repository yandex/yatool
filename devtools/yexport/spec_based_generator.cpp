#include "spec_based_generator.h"
#include "yexport_spec.h"

#include <spdlog/spdlog.h>

void TSpecBasedGenerator::OnAttribute(const std::string& attribute) {
    UsedAttributes.insert(attribute);
}

void TSpecBasedGenerator::ReadYexportSpec(fs::path configDir) {
    if (!configDir.empty()) {
        auto yexportToml = configDir / YEXPORT_FILE;
        if (fs::exists(yexportToml)) {
            LoadTargetReplacements(yexportToml, TargetReplacements_);
        }
    }
}

THashSet<fs::path> TSpecBasedGenerator::CollectFilesToCopy() const {
    THashSet<fs::path> result;
    result.insert(GeneratorSpec.Root.Copy.begin(), GeneratorSpec.Root.Copy.end());

    for (auto const& [key, value] : GeneratorSpec.Attrs) {
        for (auto const& [tableName, item] : value.Items) {
            if (UsedAttributes.contains(tableName)) {
                result.insert(item.Copy.begin(), item.Copy.end());
            }
        }
    }
    return result;
}

void TSpecBasedGenerator::CopyFiles() {
    THashSet<fs::path> files = CollectFilesToCopy();
    for (const auto& path : files) {
        ExportFileManager->Copy(GeneratorDir / path, path);
    }
}
