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
    static constexpr const char* YEXPORT_FILE = "yexport.toml";///< Additional configure for yexport
    static const std::string EMPTY_TARGET;///< Magic target for use in directory without any targets
    static const std::string EXTRA_ONLY_TARGET;///< Magic target for use in directory with only extra targets without main target

    TSpecBasedGenerator() noexcept = default;
    virtual ~TSpecBasedGenerator() = default;

    const TGeneratorSpec& GetGeneratorSpec() const;
    const fs::path& GetGeneratorDir() const;
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;
    void ApplyRules(TAttrsPtr attrs) const;
    jinja2::TemplateEnv* GetJinjaEnv() const;
    void SetCurrentDirectory(const fs::path& dir) const;

    void SetupJinjaEnv();
    void OnAttribute(const std::string& attrName, const std::span<const std::string>& attrValue);
    void OnPlatform(const std::string& platformName);

    const TDumpOpts& DumpOpts() const {
        return DumpOpts_;
    }
    const TDebugOpts& DebugOpts() const {
        return DebugOpts_;
    }

    TAttrsPtr MakeAttrs(EAttrGroup eattrGroup, const std::string& name, const TAttrs::TReplacer* toolGetter = nullptr, bool listObjectIndexing = false) const;
    bool IgnorePlatforms() const override;///< Generator ignore platforms and wait strong one sem-graph as input
    void SetSpec(const TGeneratorSpec& spec, const std::string& generatorFile = {});
    virtual const TAttrs::TReplacer* GetToolGetter() const { return nullptr; }

    void Copy(const fs::path& srcPath, const fs::path& dstRelPath);

    static fs::path yexportTomlPath(fs::path configDir) {
        return configDir / YEXPORT_FILE;
    }

protected:
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

    std::vector<TJinjaTemplate> RootTemplates;
    THashMap<std::string, std::vector<TJinjaTemplate>> TargetTemplates;
    THashMap<std::string, std::vector<TJinjaTemplate>> MergePlatformTargetTemplates;

    TAttrsPtr RootAttrs;

    void CopyFilesAndResources();
    std::vector<TJinjaTemplate> LoadJinjaTemplates(const std::vector<TTemplateSpec>& templateSpecs) const;
    void RenderJinjaTemplates(TAttrsPtr attrs, std::vector<TJinjaTemplate>& jinjaTemplates, const fs::path& relativeToExportRootDirname = {}, const std::string& platformName = {});
    void MergePlatforms();
    static void InsertPlatformNames(TAttrsPtr& attrs, const std::vector<TPlatformPtr>& platforms);
    void InsertPlatformConditions(TAttrsPtr& attrs, bool addDeprecated = false);
    void InsertPlatformAttrs(TAttrsPtr& attrs);
    void CommonFinalizeAttrs(TAttrsPtr& attrs, const jinja2::ValuesMap& addAttrs, bool doDebug = true);
    TYexportSpec ReadYexportSpec(fs::path configDir = "");

    TAttrs::TReplacer* Replacer_{nullptr};
    mutable std::string ReplacerBuffer_;
    const std::string& RootReplacer(const std::string& s) const;
    void InitReplacer();
    const std::string& GetMacroForTemplate(const NYexport::TProjectSubdir& dir);

private:
    TJinjaFileSystemPtr SourceTemplateFs;
    TJinjaEnvPtr JinjaEnv;

    void SetupHandmadeFunctions(TJinjaEnvPtr& JinjaEnv);
    fs::path PathByCopyLocation(ECopyLocation location) const;
    TCopySpec CollectFilesToCopy() const;

};

fs::path yexportTomlPath(fs::path configDir);

}
