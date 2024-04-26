#pragma once

#include "generator_spec.h"
#include "sem_graph.h"
#include "spec_based_generator.h"
#include "std_helpers.h"
#include "project.h"
#include "yexport_generator.h"

#include <devtools/ymake/dependency_management.h>

#include <contrib/libs/jinja2cpp/include/jinja2cpp/filesystem_handler.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>
#include <library/cpp/json/json_reader.h>

#include <spdlog/spdlog.h>

#include <util/generic/hash.h>
#include <util/generic/vector.h>

#include <filesystem>
#include <span>

namespace NYexport {

class TJinjaGenerator;

template<typename Values>
concept IterableValues = std::ranges::range<Values>;

struct TJinjaTarget : TProjectTarget {
    std::string TestModDir; ///< If target is test, here directory of module with this test inside

    bool IsTest() const {
        return !TestModDir.empty();
    }
};

class TJinjaProject : public TProject {
public:
    class TBuilder;

    TJinjaProject();
};
using TJinjaProjectPtr = TSimpleSharedPtr<TJinjaProject>;

class TJinjaGenerator : public TSpecBasedGenerator {
public:
    class TBuilder;
    static constexpr std::string_view EXCLUDES_ATTR = "excludes"; // Lists of excludes induced attributes
    static constexpr std::string_view TESTDEP_ATTR = "testdep";   // Dependency to test, if not empty, attr has path to library with test inside

    static THolder<TJinjaGenerator> Load(
        const fs::path& arcadiaRoot,
        const std::string& generator,
        const fs::path& configDir = "",
        const std::optional<TDumpOpts> dumpOpts = {},
        const std::optional<TDebugOpts> debugOpts = {}
    );

    void SetProjectName(const std::string& name) override { ProjectName = name; }
    void LoadSemGraph(const std::string& platform, const fs::path& semGraph) override;

    void AnalizeSemGraph(const TVector<TNodeId>& startDirs, const TSemGraph& graph);

    void DumpSems(IOutputStream& out) override; ///< Get dump of semantics tree with values for testing or debug
    void DumpAttrs(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing or debug

private:
    friend class TJinjaProject::TBuilder;

    void Render(ECleanIgnored cleanIgnored) override;
    void RenderSubdir(TProjectSubdirPtr subdir);

    EAttrTypes GetAttrType(EAttrGroup attrGroup, const std::string_view attrName) const;

    const jinja2::ValuesMap& FinalizeRootAttrs();
    jinja2::ValuesMap FinalizeSubdirsAttrs(const std::vector<std::string>& pathPrefixes = {});
    jinja2::ValuesMap FinalizeAttrsForDump();

    std::string ProjectName;

    std::vector<TJinjaTemplate> RootTemplates;
    std::vector<TJinjaTemplate> DirTemplates;
    std::vector<TJinjaTemplate> CommonTemplates;
    THashMap<std::string, std::vector<TJinjaTemplate>> TargetTemplates;

    TProjectPtr Project;
    TAttrsPtr RootAttrs;

public: // for tests only
    THashMap<fs::path, TVector<TJinjaTarget>> GetSubdirsTargets() const;
    void SetSpec(const TGeneratorSpec& spec);
};

class TJinjaProject::TBuilder : public TProject::TBuilder {
public:
    TBuilder(TJinjaGenerator* generator, TAttrsPtr rootAttrs);

    template<IterableValues Values>
    void SetRootAttr(const std::string_view attrName, const Values& values, const std::string& nodePath) {
        Y_ASSERT(RootAttrs);
        RootAttrs->SetAttrValue(attrName, values, nodePath);
    }

    template<IterableValues Values>
    void SetDirectoryAttr(const std::string_view attrName, const Values& values, const std::string& nodePath) {
        if (!CurSubdir_) {
            spdlog::error("attempt to add directory attribute '{}' while there is no active directory at node {}", attrName, nodePath);
            return;
        }
        Y_ASSERT(CurSubdir_->Attrs);
        CurSubdir_->Attrs->SetAttrValue(attrName, values, nodePath);
    }

    template<IterableValues Values>
    void SetTargetAttr(const std::string_view attrName, const Values& values, const std::string& nodePath) {
        if (!CurTarget_) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrName, nodePath);
            return;
        }
        Y_ASSERT(CurTarget_->Attrs);
        CurTarget_->Attrs->SetAttrValue(attrName, values, nodePath);
    }

    bool AddToTargetInducedAttr(const std::string& attrName, const jinja2::Value& value, const std::string& nodePath);
    void OnAttribute(const std::string& attribute);
    void SetTestModDir(const std::string& testModDir);
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;

    static void MergeTree(jinja2::ValuesMap& attrs, const jinja2::ValuesMap& tree);

private:
    using TGetDebugStr = const std::function<std::string()>&;

    static bool ValueInList(const jinja2::ValuesList& list, const jinja2::Value& val);

    TAttrsPtr RootAttrs;
    TJinjaGenerator* Generator;
};

}
