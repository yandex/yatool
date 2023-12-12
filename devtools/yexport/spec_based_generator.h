#pragma once

#include "generator_spec.h"
#include "yexport_generator.h"
#include "path_hash.h"
#include "target_replacements.h"

#include <util/generic/hash_set.h>

#include <filesystem>
#include <string>
#include <type_traits>

namespace NYexport {

/// Common base class for generators configurable with generator.toml specs
class TSpecBasedGenerator : public TYexportGenerator {
public:
    TSpecBasedGenerator() noexcept = default;
    virtual ~TSpecBasedGenerator() = default;

    const TGeneratorSpec& GetGeneratorSpec() const;
    const fs::path& GetGeneratorDir() const;
    void OnAttribute(const std::string& attribute);

    static constexpr const char* GENERATOR_FILE = "generator.toml";
    static constexpr const char* GENERATORS_ROOT = "build/export_generators";
    static constexpr const char* YEXPORT_FILE = "yexport.toml";

protected:
    void CopyFilesAndResources();

    fs::path GeneratorDir;
    TGeneratorSpec GeneratorSpec;
    THashSet<std::string> UsedAttributes;
    THashSet<const TGeneratorRule*> UsedRules;
    TTargetReplacements TargetReplacements_;

    void ReadYexportSpec(fs::path configDir = "");

private:
    THashSet<fs::path> CollectResourcesToCopy() const;
    THashSet<fs::path> CollectFilesToCopy() const;
};

}
