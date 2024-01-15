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
    static constexpr std::string_view EXCLUDES_ATTR = "excludes";   // Lists of excludes induced attributes
    static constexpr std::string_view TEST2TEST_ATTR = "test2test"; // Flag of dependency test to test

    static THolder<TJinjaGenerator> Load(const fs::path& arcadiaRoot, const std::string& generator, const fs::path& configDir = "");

    void SetProjectName(const std::string& name) override { ProjectName = name; }
    void LoadSemGraph(const std::string& platform, const fs::path& semGraph) override;

    void AnalizeSemGraph(const TVector<TNodeId>& startDirs, const TSemGraph& graph);
    THashMap<fs::path, TVector<TJinjaTarget>> GetSubdirsTargets() const;
    void SetSpec(const TGeneratorSpec& spec) { GeneratorSpec = spec; };

    void Dump(IOutputStream& out) override; ///< Get dump of attributes tree with values for testing

private:
    void Render(ECleanIgnored cleanIgnored) override;

    EAttrTypes GetAttrType(const std::string& attrGroup, const std::string& attrName) const;

private:
    friend class TJinjaProject::TBuilder;

    void RenderSubdir(const fs::path& subdir, const jinja2::ValuesMap& subdirAttrs);

    jinja2::ValuesMap FinalizeAllAttrs();
    const jinja2::ValuesMap& FinalizeRootAttrs();
    jinja2::ValuesMap FinalizeSubdirsAttrs();

    std::string ProjectName;

    std::vector<jinja2::Template> Templates;
    THashMap<std::string, std::vector<jinja2::Template>> TargetTemplates;

    TProjectPtr Project;
    jinja2::ValuesMap JinjaAttrs; // TODO: use attr storage from jinja_helpers
    bool DoDump{false};
};

class TJinjaProject::TBuilder : public TProject::TBuilder {
public:
    TBuilder(TJinjaGenerator* generator, jinja2::ValuesMap* rootAttrs);

    template<IterableValues Values>
    bool SetRootAttr(const std::string& attrName, const Values& values, const std::string& nodePath) {
        return SetAttrValue(*RootAttrs, ATTRGROUP_ROOT, attrName, values, nodePath);
    }

    template<IterableValues Values>
    bool SetTargetAttr(const std::string& attrName, const Values& values, const std::string& nodePath) {
        if (!CurTarget_) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrName, nodePath);
            return false;
        }
        return SetAttrValue(CurTarget_->Attrs, ATTRGROUP_TARGET, attrName, values, nodePath);
    }

    template<IterableValues Values>
    bool SetInducedAttr(jinja2::ValuesMap& attrs,const std::string& attrName, const Values& values, const std::string& nodePath) {
        return SetAttrValue(attrs, ATTRGROUP_INDUCED, attrName, values, nodePath);
    }

    bool AddToTargetInducedAttr(const std::string& attrName, const jinja2::Value& value, const std::string& nodePath);
    void OnAttribute(const std::string& attribute);
    void SetTestModDir(const std::string& testModDir);
    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const;

    std::tuple<std::string, jinja2::ValuesMap> MakeTreeJinjaAttrs(const std::string& attrNameWithDividers, size_t lastDivPos, jinja2::ValuesMap&& treeJinjaAttrs);

    template<IterableValues Values>
    bool SetAttrValue(jinja2::ValuesMap& jinjaAttrs, const std::string& attrGroup, const std::string& attrName, const Values& values, const std::string& nodePath, bool itemMode = false) {
        jinja2::ValuesList jvalues;
        const jinja2::ValuesList* valuesPtr;
        if constexpr(std::same_as<Values, jinja2::ValuesList>) {
            valuesPtr = &values;
        } else {
            Copy(values.begin(), values.end(), std::back_inserter(jvalues));
            valuesPtr = &jvalues;
        }
        auto attrType = Generator->GetAttrType(attrGroup, attrName);
        Y_ASSERT(attrType != EAttrTypes::Unknown);
        if (!itemMode && attrName.rfind(ITEM_TYPE) == attrName.size() - ITEM_TYPE.size()) { // attr with -ITEM tail not in item mode
            auto parentAttrName = attrName.substr(0, attrName.size() - ITEM_TYPE.size());
            if (attrType == EAttrTypes::Dict && Generator->GetAttrType(attrGroup, parentAttrName) == EAttrTypes::List) { // item is dist and parent is list
                auto [attrIt, _] = jinjaAttrs.emplace(parentAttrName, jinja2::ValuesList{});
                attrIt->second.asList().emplace_back(jinja2::ValuesMap{});
                return true;
            } else {
                spdlog::error("trying create item of not list attribute {} at node {}", parentAttrName, nodePath);
                return false;
            }
        }
        size_t lastDivPos = itemMode ? std::string::npos : attrName.rfind(ATTR_DIVIDER);
        // If attrName has attribute divider use temp treeJinjaAttrs for set attr
        jinja2::ValuesMap tempJinjaAttrs;
        std::string treeAttrName;
        jinja2::ValuesMap& setJinjaAttrs = lastDivPos == std::string::npos ? jinjaAttrs : tempJinjaAttrs;
        const std::string& setAttrName = lastDivPos == std::string::npos ? attrName : attrName.substr(lastDivPos + 1);
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
                r = AppendToListAttr(setJinjaAttrs, attrGroup, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::Set:
                r = AppendToSetAttr(setJinjaAttrs, attrGroup, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::SortedSet:
                r = AppendToSortedSetAttr(setJinjaAttrs, attrGroup, setAttrName, *valuesPtr, nodePath);
                break;
            case EAttrTypes::Dict:
                r = AppendToDictAttr(setJinjaAttrs, attrGroup, setAttrName, *valuesPtr, nodePath);
                break;
            default:
                spdlog::error("Unknown attribute {} type at node {}", attrName, nodePath);
        }
        if (!r || lastDivPos == std::string::npos) {
            return r;
        }

        // Convert attrName with dividers to upperAttrName and tree of attributes base on ValuesMap
        auto [upperAttrName, treeJinjaAttrs] = MakeTreeJinjaAttrs(attrName, lastDivPos, std::move(tempJinjaAttrs));
        auto upperAttrType = Generator->GetAttrType(attrGroup, upperAttrName);
        if (upperAttrType == EAttrTypes::Dict) {
            // Merge result tree attrubutes to jinjaAttrs
            MergeTreeToAttr(jinjaAttrs, upperAttrName, treeJinjaAttrs);
        } else if (upperAttrType == EAttrTypes::List) {
            auto upperIt = jinjaAttrs.find(upperAttrName);
            if (upperIt == jinjaAttrs.end() || !upperIt->second.isList()) {
                spdlog::error("Not found upper list attribute {} for merging at node {}", upperAttrName, nodePath);
                return false;
            }
            auto& upperList = upperIt->second.asList();
            if (upperList.empty()) {
                spdlog::error("Try merge to empty upper list attribute {} at node {}", upperAttrName, nodePath);
                return false;
            }
            // Merge result tree attrubutes to last item
            MergeTree(upperList.back().asMap(), treeJinjaAttrs);
        } else {
            spdlog::error("Unknown upper attribute {} type '{}' at node {}", upperAttrName, ToString<EAttrTypes>(upperAttrType), nodePath);
            return false;
        }
        return true;
    }

    static void MergeTree(jinja2::ValuesMap& attrs, const jinja2::ValuesMap& tree);

private:
    bool ValueInList(const jinja2::ValuesList& list, const jinja2::Value& val);
    bool SetStrAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath);
    bool SetBoolAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath);
    bool SetFlagAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath);
    jinja2::Value GetItemValue(const std::string& attrGroup, const std::string& attrName, const jinja2::Value& value, const std::string& nodePath);
    bool AppendToListAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string&);
    bool AppendToSetAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string&);
    bool AppendToSortedSetAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath);
    bool AppendToDictAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath);
    void MergeTreeToAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesMap& tree);

    jinja2::ValuesMap* RootAttrs;
    TJinjaGenerator* Generator;
};

}
