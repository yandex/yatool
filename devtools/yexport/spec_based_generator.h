#pragma once

#include "generator_spec.h"
#include "yexport_generator.h"
#include "path_hash.h"
#include "target_replacements.h"

#include <util/generic/hash_set.h>

#include <filesystem>
#include <string>
#include <type_traits>

/// Common base class for generators configurable with generator.toml specs
class TSpecBasedGenerator : public TYexportGenerator {
public:
    TSpecBasedGenerator() noexcept = default;
    virtual ~TSpecBasedGenerator() = default;

    void OnAttribute(const std::string& attribute);

    static constexpr const char* GENERATOR_FILE = "generator.toml";
    static constexpr const char* GENERATORS_ROOT = "build/export_generators";
    static constexpr const char* YEXPORT_FILE = "yexport.toml";

protected:
    void CopyFiles();

    fs::path GeneratorDir;
    TGeneratorSpec GeneratorSpec;
    THashSet<std::string> UsedAttributes;
    TTargetReplacements TargetReplacements_;

    void ReadYexportSpec(fs::path configDir = "");

private:
    THashSet<fs::path> CollectFilesToCopy() const;
};
