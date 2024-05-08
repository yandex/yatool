#pragma once

#include "generator_spec.h"
#include "yexport_generator.h"
#include "std_helpers.h"
#include "target_replacements.h"
#include "attributes.h"
#include "jinja_template.h"
#include "dir_cleaner.h"
#include "dump.h"
#include "debug.h"
#include "project.h"

#include <util/generic/hash_set.h>

#include <filesystem>
#include <string>
#include <type_traits>

namespace NYexport {

struct TPlatform {
    std::string Name;
    THolder<TSemGraph> Graph;
    TVector<TNodeId> StartDirs;
    THashMap<std::string, TVector<std::string>> GlobalVars;
    TProjectPtr Project;

    TPlatform() = delete;
    explicit TPlatform(std::string_view platformName)
        : Name(platformName)
    {}
};

using TPlatformPtr = TSimpleSharedPtr<TPlatform>;

/// Common base class for generators configurable with generator.toml specs
class TSpecBasedGenerator : public TYexportGenerator {
public:
    static constexpr const char* GENERATOR_FILE = "generator.toml";
    static constexpr const char* GENERATORS_ROOT = "build/export_generators";
    static constexpr const char* GENERATOR_TEMPLATES_PREFIX = "[generator]/";
    static constexpr const char* YEXPORT_FILE = "yexport.toml";
    static constexpr const char* DEBUG_SEMS_ATTR = "dump_sems";
    static constexpr const char* DEBUG_ATTRS_ATTR = "dump_attrs";
    static constexpr const char* EMPTY_TARGET = "EMPTY";///< Magic target for use in directory without any targets

    TSpecBasedGenerator() noexcept = default;
    virtual ~TSpecBasedGenerator() = default;

    const TGeneratorSpec& GetGeneratorSpec() const;
    const fs::path& GetGeneratorDir() const;
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;
    void ApplyRules(TAttrsPtr attrs) const;
    jinja2::TemplateEnv* GetJinjaEnv() const;
    void SetCurrentDirectory(const fs::path& dir) const;

    void SetupJinjaEnv();
    void OnAttribute(const TAttr& attribute);
    void OnPlatform(const std::string_view& platform);

    const TDumpOpts& DumpOpts() const {
        return DumpOpts_;
    }
    const TDebugOpts& DebugOpts() const {
        return DebugOpts_;
    }

    TAttrsPtr MakeAttrs(EAttrGroup eattrGroup, const std::string& name) const;
    bool IgnorePlatforms() const override;///< Generator ignore platforms and wait strong one sem-graph as input

protected:
    void CopyFilesAndResources();
    std::vector<TJinjaTemplate> LoadJinjaTemplates(const std::vector<TTemplateSpec>& templateSpecs) const;
    void RenderJinjaTemplates(TAttrsPtr valuesMap, std::vector<TJinjaTemplate>& jinjaTemplates, const fs::path& relativeToExportRootDirname = {}, const std::string& platformName = {});
    void MergePlatforms(const std::vector<TJinjaTemplate>& dirTemplates, std::vector<TJinjaTemplate>& commonTemplates) const;
    static void InsertPlatforms(jinja2::ValuesMap& valuesMap, const std::vector<TPlatformPtr>& platforms);

    using TJinjaFileSystemPtr = std::shared_ptr<jinja2::RealFileSystem>;
    using TJinjaEnvPtr = std::unique_ptr<jinja2::TemplateEnv>;

    fs::path GeneratorDir;
    fs::path ArcadiaRoot;

    TGeneratorSpec GeneratorSpec;
    TYexportSpec YexportSpec;
    TVector<TPlatformPtr> Platforms;
    TDirCleaner Cleaner;
    THashSet<std::string> UsedAttributes;
    THashSet<const TGeneratorRule*> UsedRules;
    TTargetReplacements TargetReplacements_;///< Patches for semantics by path
    TDumpOpts DumpOpts_;///< Dump options for semantics and template attributes
    TDebugOpts DebugOpts_;///< Debug options for semantics and template attributes

    TYexportSpec ReadYexportSpec(fs::path configDir = "");

private:
    TJinjaFileSystemPtr SourceTemplateFs;
    TJinjaEnvPtr JinjaEnv;

    fs::path PathByCopyLocation(ECopyLocation location) const;
    TCopySpec CollectFilesToCopy() const;
};

}
