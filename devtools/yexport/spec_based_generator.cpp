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
    TFlatAttribute flatAttribute(attribute);
    for (const auto& attr : flatAttribute.BottomUpRange()) {
        UsedAttributes.emplace(attr);
        auto rules = GeneratorSpec.GetRules(std::string(attr));
        UsedRules.insert(rules.begin(), rules.end());
    }
}

void TSpecBasedGenerator::ApplyRules(TTargetAttributes& jinjaTemplate) const {
    for (const auto& rule : UsedRules) {
        for (const auto& [attr, values] : rule->AddValues) {
            jinjaTemplate.AppendAttrValue(attr, values);
        }
    }
}

const TNodeSemantics& TSpecBasedGenerator::ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
    return TargetReplacements_.ApplyReplacement(path, inputSem);
}


void TSpecBasedGenerator::ReadYexportSpec(fs::path configDir) {
    if (!configDir.empty()) {
        auto yexportToml = configDir / YEXPORT_FILE;
        if (fs::exists(yexportToml)) {
            LoadTargetReplacements(yexportToml, TargetReplacements_);
        }
    }
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
}
