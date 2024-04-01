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
    THashMap<fs::path, TVector<TJinjaTarget>> GetSubdirsTargets() const;
    void SetSpec(const TGeneratorSpec& spec) { GeneratorSpec = spec; };

    void DumpSems(IOutputStream& out) override; ///< Get dump of semantics tree with values for testing or debug
    void DumpAttrs(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing or debug

private:
    friend class TJinjaProject::TBuilder;

    void Render(ECleanIgnored cleanIgnored) override;
    void RenderSubdir(const fs::path& subdir, const jinja2::ValuesMap& subdirAttrs);

    EAttrTypes GetAttrType(EAttributeGroup attrGroup, const std::string_view attrName) const;

    const jinja2::ValuesMap& FinalizeRootAttrs();
    jinja2::ValuesMap FinalizeSubdirsAttrs(const std::vector<std::string>& pathPrefixes = {});
    jinja2::ValuesMap FinalizeAttrsForDump();

    std::string ProjectName;

    std::vector<jinja2::Template> Templates;
    THashMap<std::string, std::vector<jinja2::Template>> TargetTemplates;

    TProjectPtr Project;
    jinja2::ValuesMap RootAttrs; // TODO: use attr storage from jinja_helpers
};

class TJinjaProject::TBuilder : public TProject::TBuilder {
public:
    TBuilder(TJinjaGenerator* generator, jinja2::ValuesMap* rootAttrs);

    template<IterableValues Values>
    void SetRootAttr(const std::string_view attrName, const Values& values, const std::string& nodePath) {
        SetAttrValue(*RootAttrs, EAttributeGroup::Root, attrName, values, nodePath);
    }

    template<IterableValues Values>
    void SetTargetAttr(const std::string_view attrName, const Values& values, const std::string& nodePath) {
        if (!CurTarget_) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrName, nodePath);
            return;
        }
        SetAttrValue(CurTarget_->Attrs, EAttributeGroup::Target, attrName, values, nodePath);
    }

    template<IterableValues Values>
    void SetInducedAttr(jinja2::ValuesMap& attrs, const std::string_view attrName, const Values& values, const std::string& nodePath) {
        SetAttrValue(attrs, EAttributeGroup::Induced, attrName, values, nodePath);
    }

    bool AddToTargetInducedAttr(const std::string& attrName, const jinja2::Value& value, const std::string& nodePath);
    void OnAttribute(const std::string& attribute);
    void SetTestModDir(const std::string& testModDir);
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;

    std::tuple<std::string_view, jinja2::ValuesMap> MakeTreeJinjaAttrs(const std::string_view attrNameWithDividers, size_t lastDivPos, jinja2::ValuesMap&& treeJinjaAttrs);

    template<IterableValues Values>
    void SetAttrValue(jinja2::ValuesMap& attrs, EAttributeGroup attrGroup, const std::string_view attrName, const Values& values, const std::string& nodePath) const {
        // Convert values to jinja2::ValuesList
        jinja2::ValuesList jvalues;
        const jinja2::ValuesList* valuesPtr;
        if constexpr(std::same_as<Values, jinja2::ValuesList>) {
            valuesPtr = &values;
        } else {
            Copy(values.begin(), values.end(), std::back_inserter(jvalues));
            valuesPtr = &jvalues;
        }
        TAttribute attribute(attrName);
        SetAttrValue(attrs, attrGroup, attribute, 0, *valuesPtr, [&]() {
            return "attribute " + attribute.str() + " at node " + nodePath;// debug string for error messages
        });
    }

    static void MergeTree(jinja2::ValuesMap& attrs, const jinja2::ValuesMap& tree);

private:
    using TGetDebugStr = const std::function<std::string()>&;

    static bool ValueInList(const jinja2::ValuesList& list, const jinja2::Value& val);

    void SetAttrValue(jinja2::ValuesMap& attrs, EAttributeGroup attrGroup, const TAttribute& attribute, size_t atPos, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const;

    static jinja2::Value GetSimpleAttrValue(const EAttrTypes attrType, const jinja2::ValuesList& values, TGetDebugStr getDebugStr);
    static void SetStrAttr(jinja2::ValuesMap& attrs, const std::string_view keyName, const jinja2::Value& value, TGetDebugStr getDebugStr);
    static void SetBoolAttr(jinja2::ValuesMap& attrs, const std::string_view keyName, const jinja2::Value& value, TGetDebugStr getDebugStr);
    static void SetFlagAttr(jinja2::ValuesMap& attrs, const std::string_view keyName, const jinja2::Value& value, TGetDebugStr getDebugStr);

    void AppendToListAttr(jinja2::ValuesList& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const;
    void AppendToSetAttr(jinja2::ValuesList& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const;
    void AppendToSortedSetAttr(jinja2::ValuesList& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const;
    void AppendToDictAttr(jinja2::ValuesMap& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const;

    EAttrTypes GetItemAttrType(EAttributeGroup attrGroup, const std::string_view attrName) const;

    jinja2::ValuesMap* RootAttrs;
    TJinjaGenerator* Generator;
};

}
