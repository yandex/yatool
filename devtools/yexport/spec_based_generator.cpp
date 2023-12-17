#include "spec_based_generator.h"
#include "yexport_spec.h"

#include <spdlog/spdlog.h>

namespace NYexport {

const TGeneratorSpec& TSpecBasedGenerator::GetGeneratorSpec() const {
    return GeneratorSpec;
}

const fs::path& TSpecBasedGenerator::GetGeneratorDir() const {
    return GeneratorDir;
}

void TSpecBasedGenerator::OnAttribute(const std::string& attribute) {
    UsedAttributes.insert(attribute);
    auto rules = GeneratorSpec.GetRules(attribute);
    UsedRules.insert(rules.begin(), rules.end());
}

void TSpecBasedGenerator::ApplyRules(TTargetAttributes& jinjaTemplate) const {
    for (const auto& rule : UsedRules) {
        for (const auto& [attr, values] : rule->AddValues) {
            jinjaTemplate.AppendAttrValue(attr, values);
        }
    }
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

    for (auto rule : UsedRules) {
        result.insert(rule->Copy.begin(), rule->Copy.end());
    }

    result.insert(GeneratorSpec.Root.Copy.begin(), GeneratorSpec.Root.Copy.end());
    for (const auto& [key, value] : GeneratorSpec.Attrs) {
        for (const auto& [attrName, item] : value.Items) {
            if (UsedAttributes.contains(attrName)) {
                result.insert(item.Copy.begin(), item.Copy.end());
            }
        }
    }

    return result;
}

void TSpecBasedGenerator::CopyFilesAndResources() {
    for (const auto& path : CollectFilesToCopy()) {
        ExportFileManager->Copy(GeneratorDir / path, path);
    }
}

}
