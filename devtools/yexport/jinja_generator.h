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
    bool isTest;
};

class TJinjaProject : public TProject {
public:
    class TBuilder;

    TJinjaProject();
};
using TJinjaProjectPtr = TSimpleSharedPtr<TJinjaProject>;

class TJinjaGenerator : public TSpecBasedGenerator {
public:
    static constexpr std::string_view EXCLUDES_ATTR = "excludes";

    static THolder<TJinjaGenerator> Load(const fs::path& arcadiaRoot, const std::string& generator, const fs::path& configDir = "");

    void SetProjectName(const std::string& name) override { ProjectName = name; }
    void LoadSemGraph(const std::string& platform, const fs::path& semGraph) override;

    void AnalizeSemGraph(const TVector<TNodeId>& startDirs, const TSemGraph& graph);
    THashMap<fs::path, TVector<TJinjaTarget>> GetSubdirsTargets() const;
    void SetSpec(const TGeneratorSpec& spec) { GeneratorSpec = spec; };

    void Dump(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing
private:
    void Render(ECleanIgnored cleanIgnored) override;

    EAttrTypes GetAttrType(const std::string& attrGroup, const std::string& attrMacro) const;

private:
    friend class TJinjaProject::TBuilder;

    void RenderSubdir(const fs::path& subdir, const jinja2::ValuesMap& subdirAttrs);

    jinja2::ValuesMap FinalizeAllAttrs();
    const jinja2::ValuesMap& FinalizeRootAttrs();
    jinja2::ValuesMap FinalizeSubdirsAttrs();

    std::string ProjectName;

    std::shared_ptr<jinja2::RealFileSystem> TemplateFs = std::make_shared<jinja2::RealFileSystem>();
    std::unique_ptr<jinja2::TemplateEnv> JinjaEnv = std::make_unique<jinja2::TemplateEnv>();
    std::vector<jinja2::Template> Templates;
    THashMap<std::string, std::vector<jinja2::Template>> TargetTemplates;

    TProjectPtr Project;
    jinja2::ValuesMap JinjaAttrs; // TODO: use attr storage from jinja_helpers
};

class TJinjaProject::TBuilder : public TProject::TBuilder {
public:
    TBuilder(TJinjaGenerator* generator, jinja2::ValuesMap* rootAttrs);

    template<IterableValues Values>
    bool SetRootAttr(const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        return SetAttrValue(*RootAttrs, ATTRGROUP_ROOT, attrMacro, values, nodePath);
    }

    template<IterableValues Values>
    bool SetTargetAttr(const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        if (!CurTarget_) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrMacro, nodePath);
            return false;
        }
        return SetAttrValue(CurTarget_->Attrs, ATTRGROUP_TARGET, attrMacro, values, nodePath);
    }

    template<IterableValues Values>
    bool SetInducedAttr(jinja2::ValuesMap& attrs,const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        return SetAttrValue(attrs, ATTRGROUP_INDUCED, attrMacro, values, nodePath);
    }

    bool AddToTargetInducedAttr(const std::string& attrMacro, const jinja2::Value& value, const std::string& nodePath);
    void OnAttribute(const std::string& attribute);
    bool IsExcluded(TStringBuf path, std::span<const std::string> excludes) const noexcept;

    void SetIsTest(bool isTestTarget);
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;

    std::tuple<std::string, jinja2::ValuesMap> MakeTreeJinjaAttrs(const std::string& attrNameWithDividers, size_t lastDivPos, jinja2::ValuesMap&& treeJinjaAttrs);

    template<IterableValues Values>
    bool SetAttrValue(jinja2::ValuesMap& jinjaAttrs, const std::string& attrGroup, const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        jinja2::ValuesList jvalues;
        const jinja2::ValuesList* valuesPtr;
        if constexpr(std::same_as<Values, jinja2::ValuesList>) {
            valuesPtr = &values;
        } else {
            Copy(values.begin(), values.end(), std::back_inserter(jvalues));
            valuesPtr = &jvalues;
        }
        auto attrType = Generator->GetAttrType(attrGroup, attrMacro);
        Y_ASSERT(attrType != EAttrTypes::Unknown);
        // If attrName has attribute divider use temp treeJinjaAttrs for set attr
        auto lastDivPos = attrMacro.rfind(ATTR_DIVIDER);
        jinja2::ValuesMap tempJinjaAttrs;
        std::string treeAttrName;
        jinja2::ValuesMap& setJinjaAttrs = lastDivPos == std::string::npos ? jinjaAttrs : tempJinjaAttrs;
        const std::string& setAttrName = lastDivPos == std::string::npos ? attrMacro : attrMacro.substr(lastDivPos + 1);
        bool r = false;
        switch (attrType) {
            case EAttrTypes::Str:
                r = SetStrAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::Bool:
                r = SetBoolAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::Flag:
                r = SetFlagAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::List:
                r = AppendToListAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::Set:
                r = AppendToSetAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::SortedSet:
                r = AppendToSortedSetAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::Dict:
                r = AppendToDictAttr(setJinjaAttrs, setAttrName, *valuesPtr, nodePath);
                break;
            default:
                spdlog::error("Unknown attribute {} type at node {}", attrMacro, nodePath);
        }
        if (!r || lastDivPos == std::string::npos) {
            return r;
        }

        // Convert attrName with dividers to upperAttrName and tree of attributes base on ValuesMap
        auto [upperAttrName, treeJinjaAttrs] = MakeTreeJinjaAttrs(attrMacro, lastDivPos, std::move(tempJinjaAttrs));
        // Merge result tree attrubutes to jinjaAttrs
        MergeTreeToAttr(jinjaAttrs, upperAttrName, treeJinjaAttrs);
        return true;
    }

private:
    bool ValueInList(const jinja2::ValuesList& list, const jinja2::Value& val);
    bool SetStrAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath);
    bool SetBoolAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath);
    bool SetFlagAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath);
    bool AppendToListAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&);
    bool AppendToSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&);
    bool AppendToSortedSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath);
    bool AppendToDictAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath);
    void MergeTreeToAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesMap& tree);
    void MergeTree(jinja2::ValuesMap& attr, const jinja2::ValuesMap& tree);

    jinja2::ValuesMap* RootAttrs;
    TJinjaGenerator* Generator;
};

}
