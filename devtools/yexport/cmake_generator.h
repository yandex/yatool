#pragma once

#include "generator_spec.h"
#include "dir_cleaner.h"
#include "jinja_template.h"
#include "spec_based_generator.h"
#include "project.h"

#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/vector.h>
#include <util/generic/set.h>

#include <filesystem>
#include <string>

#include <type_traits>

namespace NYexport {

struct TProjectConf {
    std::string ProjectName;
    fs::path ArcadiaRoot;
    ECleanIgnored CleanIgnored = ECleanIgnored::Disabled;

    TProjectConf() = default;
    TProjectConf(std::string_view name, const fs::path& arcadiaRoot, ECleanIgnored cleanIgnored = ECleanIgnored::Disabled);
};

struct TPlatform {
    std::string Name;
    THolder<TSemGraph> Graph;
    TVector<TNodeId> StartDirs;
    THashMap<std::string, TVector<std::string>> GlobalVars;
    THashSet<fs::path> SubDirs;
    TProjectPtr Project;

    explicit TPlatform(std::string_view platformName);
};

using TPlatformPtr = TSimpleSharedPtr<TPlatform>;

struct TGlobalProperties {
    TSet<std::string> Languages;
    TSet<std::string> ConanPackages;
    TSet<std::string> ConanToolPackages;
    TSet<std::string> ConanImports;
    TSet<std::string> ConanOptions;
    TSet<fs::path> GlobalModules; // module pathes
    THashSet<fs::path> ArcadiaScripts;
    bool VanillaProtobuf{false};
};

class TCMakeGenerator: public TSpecBasedGenerator {
private:
    TProjectConf Conf;
    TDirCleaner Cleaner;
    TVector<TPlatformPtr> Platforms;
    TGlobalProperties GlobalProperties;

    void RenderPlatforms();
    void RenderRoot();
    void InsertPlatforms(jinja2::ValuesMap& valuesMap, const TVector<TPlatformPtr> platforms) const;
    void MergePlatforms(const std::vector<TJinjaTemplate>& dirJinjaTemplates) const;
    jinja2::ValuesList GetAdjustedLanguagesList() const;
    void CopyArcadiaScripts() const;

    void SetArcadiaRoot(const fs::path& arcadiaRoot);
    void RenderPlatform(const TPlatformPtr platform, std::vector<TJinjaTemplate>& dirJinjaTemplates);

    void PrepareRootCMakeList(TAttrsPtr rootValueMap) const;
    void PrepareConanRequirements(TAttrsPtr rootValueMap) const;

    void Render(ECleanIgnored cleanIgnored) override;

public:
    TCMakeGenerator() = default;
    TCMakeGenerator(std::string_view name, const fs::path& arcadiaRoot);

    static THolder<TCMakeGenerator> Load(const fs::path& arcadiaRoot, const std::string& generator, const fs::path& configDir = "");

    void SetProjectName(const std::string& projectName) override;
    void LoadSemGraph(const std::string& platform, const fs::path& semGraph) override;

    void DumpSems(IOutputStream& out) override; ///< Get dump of semantics tree with values for testing or debug
    void DumpAttrs(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing or debug
};

}
