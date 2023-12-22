#pragma once

#include "generator_spec.h"
#include "yexport_generator.h"
#include "std_helpers.h"
#include "target_replacements.h"
#include "jinja_helpers.h"

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
    void ApplyRules(TTargetAttributes& map) const;

    static constexpr const char* GENERATOR_FILE = "generator.toml";
    static constexpr const char* GENERATORS_ROOT = "build/export_generators";
    static constexpr const char* YEXPORT_FILE = "yexport.toml";

protected:
    void CopyFilesAndResources();

    fs::path GeneratorDir;
    fs::path ArcadiaRoot;
    TGeneratorSpec GeneratorSpec;
    THashSet<std::string> UsedAttributes;
    THashSet<const TGeneratorRule*> UsedRules;
    TTargetReplacements TargetReplacements_;

    void ReadYexportSpec(fs::path configDir = "");

private:
    fs::path PathByCopyLocation(ECopyLocation location) const;
    TCopySpec CollectFilesToCopy() const;
};

}
