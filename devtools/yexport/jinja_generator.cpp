#include "jinja_generator.h"
#include "read_sem_graph.h"
#include "graph_visitor.h"
#include "attribute.h"

#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/common/uniq_vector.h>

#include <library/cpp/json/json_reader.h>
#include <library/cpp/resource/resource.h>

#include <util/generic/algorithm.h>
#include <util/generic/set.h>
#include <util/generic/yexception.h>
#include <util/generic/map.h>
#include <util/string/type.h>
#include <util/system/fs.h>

#include <contrib/libs/jinja2cpp/include/jinja2cpp/template.h>
#include <contrib/libs/jinja2cpp/include/jinja2cpp/template_env.h>

#include <fstream>
#include <vector>

namespace NYexport {

TJinjaProject::TJinjaProject() {
    SetFactoryTypes<TProjectSubdir, TJinjaTarget>();
}

TJinjaProject::TBuilder::TBuilder(TJinjaGenerator* generator, jinja2::ValuesMap* rootAttrs)
    : TProject::TBuilder(generator)
    , RootAttrs(rootAttrs)
    , Generator(generator)
{
    Project_ = MakeSimpleShared<TJinjaProject>();
}

static bool AddValueToJinjaList(jinja2::ValuesList& list, const jinja2::Value& value) {
    if (value.isList()) {
        // Never create list of lists, if value also list, concat all lists to one big list
        for (const auto& v: value.asList()) {
            list.emplace_back(v);
        }
    } else {
        list.emplace_back(value);
    }
    return true;
}

bool TJinjaProject::TBuilder::AddToTargetInducedAttr(const std::string& attrName, const jinja2::Value& value, const std::string& nodePath) {
    if (!CurTarget_) {
        spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrName, nodePath);
        return false;
    }
    auto [listAttrIt, _] = CurTarget_->Attrs.emplace(attrName, jinja2::ValuesList{});
    auto& list = listAttrIt->second.asList();
    if (ValueInList(list, value)) {
        return true; // skip adding fully duplicate induced attributes
    }
    return AddValueToJinjaList(list, value);
}

void TJinjaProject::TBuilder::OnAttribute(const std::string& attribute) {
    Generator->OnAttribute(attribute);
}

void TJinjaProject::TBuilder::SetTestModDir(const std::string& testModDir) {
    CurTarget_.As<TJinjaTarget>()->TestModDir = testModDir;
}

const TNodeSemantics& TJinjaProject::TBuilder::ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
    return Generator->ApplyReplacement(path, inputSem);
}

std::tuple<std::string_view, jinja2::ValuesMap> TJinjaProject::TBuilder::MakeTreeJinjaAttrs(const std::string_view attrNameWithDividers, size_t lastDivPos, jinja2::ValuesMap&& treeJinjaAttrs) {
    auto upperAttrName = attrNameWithDividers.substr(0, lastDivPos); // current attr for tree
    // Parse upperAttrName for all tree levels
    while ((lastDivPos = upperAttrName.rfind(ATTR_DIVIDER)) != std::string::npos) {
        jinja2::ValuesMap upJinjaAttrs;
        upJinjaAttrs.emplace(upperAttrName.substr(lastDivPos + 1), std::move(treeJinjaAttrs));
        treeJinjaAttrs = std::move(upJinjaAttrs);
        upperAttrName = upperAttrName.substr(0, lastDivPos);
    }
    return {upperAttrName, treeJinjaAttrs};
}

bool TJinjaProject::TBuilder::ValueInList(const jinja2::ValuesList& list, const jinja2::Value& value) {
    if (list.empty()) {
        return false;
    }
    TStringBuilder valueStr;
    TStringBuilder listValueStr;
    Dump(valueStr.Out, value);
    for (const auto& listValue : list) {
        listValueStr.clear();
        Dump(listValueStr.Out, listValue);
        if (valueStr == listValueStr) {
            return true;
        }
    }
    return false;
}

void TJinjaProject::TBuilder::SetAttrValue(jinja2::ValuesMap& attrs, EAttributeGroup attrGroup, const TAttribute& attribute, size_t atPos, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    if (atPos == (attribute.Size() - 1)) {// last item - append complex attribute or set simple attribute
        auto attrType = Generator->GetAttrType(attrGroup, attribute.GetFirstParts(atPos));
        auto keyName = attribute.GetPart(atPos);
        switch (attrType) {
            case EAttrTypes::Str:
                SetStrAttr(attrs, keyName, GetSimpleAttrValue(attrType, values, getDebugStr), getDebugStr);
                break;
            case EAttrTypes::Bool:
                SetBoolAttr(attrs, keyName, GetSimpleAttrValue(attrType, values, getDebugStr), getDebugStr);
                break;
            case EAttrTypes::Flag:
                SetFlagAttr(attrs, keyName, GetSimpleAttrValue(attrType, values, getDebugStr), getDebugStr);
                break;
            case EAttrTypes::List:{
                auto [listAttrIt, _] = attrs.emplace(keyName, jinja2::ValuesList{});
                AppendToListAttr(listAttrIt->second.asList(), attrGroup, attribute, values, getDebugStr);
            }; break;
            case EAttrTypes::Set:{
                auto [setAttrIt, _] = attrs.emplace(keyName, jinja2::ValuesList{});
                AppendToSetAttr(setAttrIt->second.asList(), attrGroup, attribute, values, getDebugStr);
            }; break;
            case EAttrTypes::SortedSet:{
                auto [sortedSetAttrIt, _] = attrs.emplace(keyName, jinja2::ValuesList{});
                AppendToSortedSetAttr(sortedSetAttrIt->second.asList(), attrGroup, attribute, values, getDebugStr);
            }; break;
            case EAttrTypes::Dict:{
                auto [dictAttrIt, _] = attrs.emplace(keyName, jinja2::ValuesMap{});
                AppendToDictAttr(dictAttrIt->second.asMap(), attrGroup, attribute, values, getDebugStr);
            }; break;
            default:
                spdlog::error("Skipped unknown {}", getDebugStr());
        }
    } else { // middle item - create list/dict attribute
        auto attrType = Generator->GetAttrType(attrGroup, attribute.GetFirstParts(atPos));
        auto keyName = attribute.GetPart(atPos);
        switch (attrType) {
            case EAttrTypes::List:{// only special case may be here - list of dicts
                auto listItem = std::string{attribute.GetFirstParts(atPos)} + ITEM_SUFFIX;
                auto itemAttrType = Generator->GetAttrType(attrGroup, std::string{listItem});
                if (itemAttrType != EAttrTypes::Dict) {
                    spdlog::error("trying create middle item {} which is list of {} at {}", keyName, ToString<EAttrTypes>(itemAttrType), getDebugStr());
                    break;
                }
                auto [listAttrIt, _] = attrs.emplace(keyName, jinja2::ValuesList{});
                auto& list = listAttrIt->second.asList();
                if (listItem == attribute.str()) {// magic attribute <list>-ITEM must append new empty item
                    list.emplace_back(jinja2::ValuesMap{});
                    break;
                }
                if (list.empty()) {
                    spdlog::error("trying set item of empty list {} at {}", keyName, getDebugStr());
                    break;
                }
                // Always apply attributes to last item of list
                SetAttrValue(list.back().asMap(), attrGroup, attribute, atPos + 1, values, getDebugStr);
            }; break;
            case EAttrTypes::Dict:{
                auto [dictAttrIt, _] = attrs.emplace(keyName, jinja2::ValuesMap{});
                SetAttrValue(dictAttrIt->second.asMap(), attrGroup, attribute, atPos + 1, values, getDebugStr);
            }; break;
            default:
                spdlog::error("Can't create middle {} for type {} of {}", keyName, ToString<EAttrTypes>(attrType), getDebugStr());
        }
    }
}

jinja2::Value TJinjaProject::TBuilder::GetSimpleAttrValue(const EAttrTypes attrType, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) {
    switch (attrType) {
        case EAttrTypes::Str:
            if (values.size() > 1) {
                spdlog::error("trying to add {} elements to 'str' type {}, type 'str' should have only 1 element", values.size(), getDebugStr());
                // but continue and use first item
            }
            return values.empty() ? std::string{} : values[0].asString();
        case EAttrTypes::Bool:
            if (values.size() > 1) {
                spdlog::error("trying to add {} elements to 'bool' type {}, type 'bool' should have only 1 element", values.size(), getDebugStr());
                // but continue and use first item
            }
            return values.empty() ? false : IsTrue(values[0].asString());
        case EAttrTypes::Flag:
            if (values.size() > 0) {
                spdlog::error("trying to add {} elements to 'flag' type {}, type 'flag' should have only 0 element", values.size(), getDebugStr());
                // but continue
            }
            return true;
        default:
            spdlog::error("try get simple value of {} with type {}", ToString<EAttrTypes>(attrType), getDebugStr());
            return {};// return empty value
    }
}

void TJinjaProject::TBuilder::SetStrAttr(jinja2::ValuesMap& attrs, const std::string_view keyName, const jinja2::Value& value, TGetDebugStr getDebugStr) {
    const auto& v = value.asString();
    const auto [attrIt, inserted] = attrs.emplace(keyName, value);
    if (!inserted && attrIt->second.asString() != v) {
        spdlog::error("Set string value '{}' of {}, but it already has value '{}', overwritten", v, getDebugStr(), attrIt->second.asString());
        attrIt->second = value;
    }
}

void TJinjaProject::TBuilder::SetBoolAttr(jinja2::ValuesMap& attrs, const std::string_view keyName, const jinja2::Value& value, TGetDebugStr getDebugStr) {
    const auto v = value.get<bool>();
    const auto [attrIt, inserted] = attrs.emplace(keyName, value);
    if (!inserted && attrIt->second.get<bool>() != v) {
        spdlog::error("Set bool value {} of {}, but it already has value {}, overwritten", v ? "True" : "False", getDebugStr(), attrIt->second.get<bool>() ? "True" : "False");
        attrIt->second = value;
    }
}

void TJinjaProject::TBuilder::SetFlagAttr(jinja2::ValuesMap& attrs, const std::string_view keyName, const jinja2::Value& value, TGetDebugStr /*getDebugStr*/) {
    attrs.insert_or_assign(std::string{keyName}, value);
}

void TJinjaProject::TBuilder::AppendToListAttr(jinja2::ValuesList& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    auto itemAttrType = GetItemAttrType(attrGroup, attribute.str());
    for (const auto& value : values) {
        auto item = GetSimpleAttrValue(itemAttrType, jinja2::ValuesList{value}, getDebugStr);
        if (item.isEmpty()) {
            continue;
        }
        attr.emplace_back(std::move(item));
    }
}

void TJinjaProject::TBuilder::AppendToSetAttr(jinja2::ValuesList& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    auto itemAttrType = GetItemAttrType(attrGroup, attribute.str());
    for (const auto& value : values) {
        auto item = GetSimpleAttrValue(itemAttrType, jinja2::ValuesList{value}, getDebugStr);
        if (item.isEmpty()) {
            continue;
        }
        bool exists = false;
        for (const auto& v: attr) {
            if (exists |= v.asString() == item.asString()) {
                break;
            }
        }
        if (!exists) { // add to list only if not exists
            attr.emplace_back(std::move(item));
        }
    }
}

void TJinjaProject::TBuilder::AppendToSortedSetAttr(jinja2::ValuesList& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    auto itemAttrType = GetItemAttrType(attrGroup, attribute.str());
    std::set<std::string> set;
    if (!attr.empty()) {
        for (const auto& item : attr) { //fill set by exists values
            set.emplace(item.asString());
        }
    }
    for (const auto& value : values) {
        auto item = GetSimpleAttrValue(itemAttrType, jinja2::ValuesList{value}, getDebugStr);
        if (item.isEmpty()) {
            continue;
        }
        set.emplace(item.asString());// append new values
    }
    attr.clear();
    for (const auto& item : set) { // full refill attr from set
        attr.emplace_back(item);
    }
}

void TJinjaProject::TBuilder::AppendToDictAttr(jinja2::ValuesMap& attr, EAttributeGroup attrGroup, const TAttribute& attribute, const jinja2::ValuesList& values, TGetDebugStr getDebugStr) const {
    for (const auto& value : values) {
        auto keyval = std::string_view(value.asString());
        if (auto pos = keyval.find_first_of('='); pos == std::string_view::npos) {
            spdlog::error("trying to add invalid element {} to dict {}, each element must be in key=value format without spaces around =", keyval, getDebugStr());
        } else {
            auto key = keyval.substr(0, pos);
            auto keyAttrType = Generator->GetAttrType(attrGroup, attribute.str() + std::string{ATTR_DIVIDER} + std::string{key});
            auto val = GetSimpleAttrValue(keyAttrType, jinja2::ValuesList{std::string{keyval.substr(pos + 1)}}, getDebugStr);
            if (val.isEmpty()) {
                continue;
            }
            attr.insert_or_assign(std::string{key}, std::move(val));
        }
    }
}

EAttrTypes TJinjaProject::TBuilder::GetItemAttrType(EAttributeGroup attrGroup, const std::string_view attrName) const {
    return Generator->GetAttrType(attrGroup, std::string{attrName} + ITEM_SUFFIX);
}

void TJinjaProject::TBuilder::MergeTree(jinja2::ValuesMap& attrs, const jinja2::ValuesMap& tree) {
    for (auto& [attrName, attrValue]: tree) {
        if (attrValue.isMap()) {
            auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesMap{});
            MergeTree(attrIt->second.asMap(), attrValue.asMap());
        } else {
            if (attrs.contains(attrName)) {
                spdlog::error("overwrite dict element {}", attrName);
            }
            attrs[attrName] = attrValue;
        }
    }
}

class TNewJinjaGeneratorVisitor: public TGraphVisitor {
public:
    TNewJinjaGeneratorVisitor(TJinjaGenerator* generator, jinja2::ValuesMap* rootAttrs, const TGeneratorSpec& generatorSpec)
        : TGraphVisitor(generator)
    {
        JinjaProjectBuilder_ = MakeSimpleShared<TJinjaProject::TBuilder>(generator, rootAttrs);
        ProjectBuilder_ = JinjaProjectBuilder_;

        if (const auto* tests = generatorSpec.Merge.FindPtr("test")) {
            for (const auto& item : *tests) {
                TestSubdirs_.push_back(item.c_str());
            }
        }
    }

    void OnTargetNodeSemantic(TState& state, const std::string& semName, const std::span<const std::string>& semArgs) override {
        bool isTestTarget = false;
        std::string modDir = semArgs[0].c_str();
        for (const auto& testSubdir: TestSubdirs_) {
            if (modDir != testSubdir && modDir.ends_with(testSubdir)) {
                modDir.resize(modDir.size() - testSubdir.size());
                isTestTarget = true;
                break;
            }
        }
        auto macroArgs = semArgs.subspan(2);
        state.Top().CurTargetHolder = ProjectBuilder_->CreateTarget(modDir);
        if (isTestTarget) {
            JinjaProjectBuilder_->SetTestModDir(modDir);
        }
        auto* curTarget = ProjectBuilder_->CurrentTarget();
        curTarget->Name = semArgs[1];
        curTarget->Macro = semName;
        curTarget->MacroArgs = {macroArgs.begin(), macroArgs.end()};
        const auto* jinjaTarget = dynamic_cast<const TJinjaTarget*>(JinjaProjectBuilder_->CurrentTarget());
        Mod2Target_.emplace(state.TopNode().Id(), jinjaTarget);
    }

    void OnNodeSemanticPostOrder(TState& state, const std::string& semName, ESemNameType semNameType, const std::span<const std::string>& semArgs) override {
        const TSemNodeData& data = state.TopNode().Value();
        if (semNameType == ESNT_RootAttr) {
            JinjaProjectBuilder_->SetRootAttr(semName, semArgs, data.Path);
        } else if (semNameType == ESNT_TargetAttr) {
            JinjaProjectBuilder_->SetTargetAttr(semName, semArgs, data.Path);
        } else if (semNameType == ESNT_InducedAttr) {
            StoreInducedAttrValues(state.TopNode().Id(), semName, semArgs, data.Path);
        } else if (semNameType == ESNT_Unknown) {
            spdlog::error("Skip unknown semantic '{}' for file '{}'", semName, data.Path);
        }
    }

    void OnLeft(TState& state) override {
        const auto& dep = state.Top().CurDep();
        if (IsDirectPeerdirDep(dep)) {
            //Note: This part checks dependence of the test on the library in the same dir, because for java we should not distribute attributes
            bool isSameDir = false;
            const auto* toTarget = Mod2Target_[dep.To().Id()];
            if (auto* curSubdir = ProjectBuilder_->CurrentSubdir(); curSubdir) {
                for (const auto& target: curSubdir->Targets) {
                    if (target.Get() == toTarget) {
                        isSameDir = true;
                        break;
                    }
                }
            }
            const auto libIt = InducedAttrs_.find(dep.To().Id());
            if (!isSameDir && libIt != InducedAttrs_.end()) {
                const TSemNodeData& data = dep.To().Value();
/*
    Generating induced attributes and excludes< for example, project is

    prog --> lib1 with excluding ex1
         \-> lib2 with excluding ex2

    And toml is:

    [attrs.induced]
    consumer-classpath="str"
    consumer-jar="str"

    For prog must be attributes map:
    ...
    consumer: [
        {
            classpath: "lib1",
            jar: "lib1.jar",
            excludes: {
                consumer: [
                    {
                        classpath: "ex1",
                        jar: "ex1.jar",
                    }
                ],
            },
        },
        {
            classpath: "lib2",
            jar: "lib2.jar",
            excludes: {
                consumer: [
                    {
                        classpath: "ex2",
                        jar: "ex2.jar",
                    }
                ],
            },
        },
    ]
    ...
*/
                // Make excludes map for each induced library
                jinja2::ValuesMap excludes;
                // Extract excludes node ids from current dependence
                if (const TSemDepData* depData = reinterpret_cast<const TSemGraph&>(dep.Graph()).GetDepData(dep); depData) {
                    if (const auto excludesIt = depData->find(DEPATTR_EXCLUDES); excludesIt != depData->end()) {
                        TVector<TNodeId> excludeNodeIds;
                        TVector<TNodeId> excludeNodeIdsWOInduced;
                        Y_ASSERT(excludesIt->second.isList());
                        // Collect all excluded node ids and collect ids without induced attributes
                        for (const auto& excludeNodeVal : excludesIt->second.asList()) {
                            const auto excludeNodeId = static_cast<TNodeId>(excludeNodeVal.get<int64_t>());
                            excludeNodeIds.emplace_back(excludeNodeId);
                            if (!InducedAttrs_.contains(excludeNodeId)) {
                                excludeNodeIdsWOInduced.emplace_back(excludeNodeId);
                            }
                        }
                        if (!excludeNodeIdsWOInduced.empty()) { // visit all not visited exclude nodes for make induced attributes
                            IterateAll(reinterpret_cast<const TSemGraph&>(dep.Graph()), excludeNodeIdsWOInduced, *this);
                        }
                        // for each excluded node id get all it induced attrs
                        for (auto excludeNodeId : excludeNodeIds) {
                            const auto excludeIt = InducedAttrs_.find(excludeNodeId);
                            if (excludeIt != InducedAttrs_.end()) {
                                // Put all induced attrs of excluded library to lists by induced attribute name
                                for (const auto& [attrName, value]: excludeIt->second) {
                                    auto [listIt, _] = excludes.emplace(attrName, jinja2::ValuesList{});
                                    AddValueToJinjaList(listIt->second.asList(), value);
                                }
                            } else {
                                spdlog::error("Not found induced for excluded node id {} at {}", excludeNodeId, data.Path);
                            }
                        }
                    }
                }
                const auto fromTarget = Mod2Target_[dep.From().Id()];
                bool isTestDep = fromTarget && toTarget && toTarget->IsTest();
                for (const auto& [attrName, value]: libIt->second) {
                    if (value.isMap() && (!excludes.empty() || isTestDep)) {
                        // For each induced attribute in map format add submap with excludes
                        jinja2::ValuesMap valueWithDepAttrs = value.asMap();
                        if (!excludes.empty()) {
                            valueWithDepAttrs.emplace(TJinjaGenerator::EXCLUDES_ATTR, excludes);
                        }
                        if (isTestDep) {
                            valueWithDepAttrs.emplace(TJinjaGenerator::TESTDEP_ATTR, toTarget->TestModDir);
                        }
                        JinjaProjectBuilder_->AddToTargetInducedAttr(attrName, valueWithDepAttrs, data.Path);
                    } else {
                        JinjaProjectBuilder_->AddToTargetInducedAttr(attrName, value, data.Path);
                    }
                }
            }
        }
    }

private:
    template<IterableValues Values>
    void StoreInducedAttrValues(TNodeId nodeId, const std::string& attrName, const Values& values, const std::string& nodePath) {
        auto [nodeIt, _] = InducedAttrs_.emplace(nodeId, jinja2::ValuesMap{});
        JinjaProjectBuilder_->SetInducedAttr(nodeIt->second, attrName, values, nodePath);
    }

    TSimpleSharedPtr<TJinjaProject::TBuilder> JinjaProjectBuilder_;

    THashMap<TNodeId, const TJinjaTarget*> Mod2Target_;
    THashMap<TNodeId, jinja2::ValuesMap> InducedAttrs_;
    TVector<std::string> TestSubdirs_;
};

THolder<TJinjaGenerator> TJinjaGenerator::Load(
    const fs::path& arcadiaRoot,
    const std::string& generator,
    const fs::path& configDir,
    const std::optional<TDumpOpts> dumpOpts,
    const std::optional<TDebugOpts> debugOpts
) {
    const auto generatorDir = arcadiaRoot / GENERATORS_ROOT / generator;
    const auto generatorFile = generatorDir / GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        YEXPORT_THROW(fmt::format("Failed to load generator {}. No {} file found", generator, generatorFile.c_str()));
    }
    THolder<TJinjaGenerator> result = MakeHolder<TJinjaGenerator>();
    if (dumpOpts.has_value()) {
        result->DumpOpts_ = dumpOpts.value();
    }
    if (debugOpts.has_value()) {
        result->DebugOpts_ = debugOpts.value();
    }

    result->GeneratorSpec = ReadGeneratorSpec(generatorFile);
    result->YexportSpec = result->ReadYexportSpec(configDir);
    result->GeneratorDir = generatorDir;
    result->ArcadiaRoot = arcadiaRoot;
    result->SetupJinjaEnv();

    auto setUpTemplates = [&result](const std::vector<TTemplate>& sources, std::vector<jinja2::Template>& targets){
        targets.reserve(sources.size());

        for (const auto& source : sources) {
            targets.push_back(jinja2::Template(result->GetJinjaEnv()));

            auto path = result->GeneratorDir / source.Template;
            std::ifstream file(path);
            if (!file.good()) {
                YEXPORT_THROW("Failed to open jinja template: " << path.c_str());
            }
            auto res = targets.back().Load(file, path);
            if (!res.has_value()) {
                spdlog::error("Failed to load jinja template due: {}", res.error().ToString());
            }
        }
    };

    result->GetJinjaEnv()->GetSettings().cacheSize = 0;
    result->GetJinjaEnv()->AddGlobal("split", jinja2::UserCallable{
        /*fptr=*/[](const jinja2::UserCallableParams& params) -> jinja2::Value {
            Y_ASSERT(params["str"].isString());
            auto str = params["str"].asString();
            Y_ASSERT(params["delimeter"].isString());
            auto delimeter = params["delimeter"].asString();
            jinja2::ValuesList list;
            size_t bpos = 0;
            size_t dpos;
            while ((dpos = str.find(delimeter, bpos)) != std::string::npos) {
                list.emplace_back(str.substr(bpos, dpos - bpos));
                bpos = dpos + delimeter.size();
            }
            list.emplace_back(str.substr(bpos));
            return list;
        },
        /*argsInfos=*/ { jinja2::ArgInfo{"str"}, jinja2::ArgInfo{"delimeter", false, " "} }
    });

    setUpTemplates(result->GeneratorSpec.Root.Templates, result->Templates);

    if (result->GeneratorSpec.Targets.empty()) {
        std::string message = "[error] No targets exist in the generator file: ";
        throw TBadGeneratorSpec(message.append(generatorFile));
    }

    for (const auto& [targetName, target]: result->GeneratorSpec.Targets) {
        if (!result->TargetTemplates.contains(targetName)) {
            std::vector<jinja2::Template> value;
            setUpTemplates(target.Templates, value);
            result->TargetTemplates.emplace(targetName, value);
        }
    }

    return result;
}

void TJinjaGenerator::AnalizeSemGraph(const TVector<TNodeId>& startDirs, const TSemGraph& graph) {
    TNewJinjaGeneratorVisitor visitor(this, &RootAttrs, GeneratorSpec);
    IterateAll(graph, startDirs, visitor);
    Project = visitor.TakeFinalizedProject();
}

void TJinjaGenerator::LoadSemGraph(const std::string&, const fs::path& semGraph) {
    const auto [graph, startDirs] = ReadSemGraph(semGraph, GeneratorSpec.UseManagedPeersClosure);
    AnalizeSemGraph(startDirs, *graph);
}

EAttrTypes TJinjaGenerator::GetAttrType(EAttributeGroup attrGroup, const std::string_view attrName) const {
    if (const auto* attrs = GeneratorSpec.AttrGroups.FindPtr(attrGroup)) {
        if (const auto it = attrs->find(attrName); it != attrs->end()) {
            return it->second;
        }
    }
    return EAttrTypes::Unknown;
}

/// Get dump of attributes tree with values for testing or debug
void TJinjaGenerator::DumpSems(IOutputStream& out) {
    if (DumpOpts_.DumpPathPrefixes.empty()) {
        out << "--- ROOT\n" << Project->SemsDump;
    }
    for (const auto& subdir: Project->GetSubdirs()) {
        if (subdir->Targets.empty()) {
            continue;
        }
        const std::string path = subdir->Path;
        if (!DumpOpts_.DumpPathPrefixes.empty()) {
            bool dumpIt = false;
            for (const auto& prefix : DumpOpts_.DumpPathPrefixes) {
                if (path.substr(0, prefix.size()) == prefix) {
                    dumpIt = true;
                    break;
                }
            }
            if (!dumpIt) {
                continue;
            }
        }
        out << "--- DIR " << path << "\n";
        for (const auto& target: subdir->Targets) {
            out << "--- TARGET " << target->Name << "\n" << target->SemsDump;
        }
    }
}

/// Get dump of attributes tree with values for testing or debug
void TJinjaGenerator::DumpAttrs(IOutputStream& out) {
    ::NYexport::Dump(out, FinalizeAttrsForDump());
}

void TJinjaGenerator::Render(ECleanIgnored) {
    YEXPORT_VERIFY(Project, "Cannot render because project was not yet loaded");
    CopyFilesAndResources();
    auto subdirsAttrs = FinalizeSubdirsAttrs();
    for (const auto& subdir: Project->GetSubdirs()) {
        if (subdir->Targets.empty()) {
            continue;
        }
        RenderSubdir(subdir->Path, subdirsAttrs[subdir->Path.c_str()].asMap());
    }

    const auto& rootAttrs = FinalizeRootAttrs();
    const auto& tmpls = GeneratorSpec.Root.Templates;
    for (size_t templateIndex = 0; templateIndex < tmpls.size(); templateIndex++) {
        const auto& tmpl = tmpls[templateIndex];
        //TODO: change name of result file
        jinja2::Result<TString> result = Templates[templateIndex].RenderAsString(rootAttrs);
        if (!ExportFileManager) {
            spdlog::error("Can't save render result to {}, empty ExportFileManager", tmpl.ResultName);
            continue;
        }
        if (result.has_value()) {
            auto out = ExportFileManager->Open(tmpl.ResultName);
            TString renderResult = result.value();
            out.Write(renderResult.data(), renderResult.size());
        } else {
            spdlog::error("Failed to generate {} due to jinja template error: {}", tmpl.ResultName, result.error().ToString());
        }
    }
}

void TJinjaGenerator::RenderSubdir(const fs::path& subdir, const jinja2::ValuesMap& subdirAttrs) {
    SetCurrentDirectory(ArcadiaRoot/subdir);
    const auto& tmpls = GeneratorSpec.Targets.begin()->second.Templates;
    for (size_t templateIndex = 0; templateIndex < tmpls.size(); templateIndex++){
        const auto& tmpl = tmpls[templateIndex];

        auto outPath = subdir / tmpl.ResultName;

        if (!ExportFileManager) {
            spdlog::error("Can't save render result to {}, empty ExportFileManager", outPath.c_str());
            continue;
        }
        auto out = ExportFileManager->Open(outPath);

        for (const auto& [targetName, targetNameAttrs] : subdirAttrs) {
            jinja2::Result<TString> result = TargetTemplates[targetName][templateIndex].RenderAsString(targetNameAttrs.asMap());
            if (result.has_value()) {
                TString renderResult = result.value();
                out.Write(renderResult.data(), renderResult.size());
            } else {
                spdlog::error("Failed to render jinja template to {} due to: {}", outPath.c_str(), result.error().ToString());
            }
        }
    }
}

THashMap<fs::path, TVector<TJinjaTarget>> TJinjaGenerator::GetSubdirsTargets() const {
    YEXPORT_VERIFY(Project, "Cannot get subdirs table because project was not yet loaded");
    THashMap<fs::path, TVector<TJinjaTarget>> res;
    for (const auto& subdir: Project->GetSubdirs()) {
        TVector<TJinjaTarget> targets;
        Transform(subdir->Targets.begin(), subdir->Targets.end(), std::back_inserter(targets), [&](TProjectTargetPtr target) { return *target.As<TJinjaTarget>(); });
        res.emplace(subdir->Path, targets);
    }
    return res;
}

const jinja2::ValuesMap& TJinjaGenerator::FinalizeRootAttrs() {
    YEXPORT_VERIFY(Project, "Cannot finalize root attrs because project was not yet loaded");
    if (RootAttrs.contains("projectName")) { // already finalized
        return RootAttrs;
    }
    auto [subdirsIt, subdirsInserted] = RootAttrs.emplace("subdirs", jinja2::ValuesList{});
    for (const auto& subdir: Project->GetSubdirs()) {
        if (subdir->Targets.empty()) {
            continue;
        }
        subdirsIt->second.asList().emplace_back(subdir->Path.c_str());
    }
    RootAttrs.emplace("arcadiaRoot", ArcadiaRoot);
    if (ExportFileManager) {
        RootAttrs.emplace("exportRoot", ExportFileManager->GetExportRoot());
    }
    RootAttrs.emplace("projectName", ProjectName);
    if (!YexportSpec.AddAttrsDir.empty()) {
        RootAttrs.emplace("add_attrs_dir", YexportSpec.AddAttrsDir);
    }
    if (!YexportSpec.AddAttrsTarget.empty()) {
        RootAttrs.emplace("add_attrs_target", YexportSpec.AddAttrsTarget);
    }
    if (DebugOpts_.DebugAttrs) {
        RootAttrs.emplace(DEBUG_ATTRS_ATTR, ::NYexport::Dump(RootAttrs));
    }
    if (DebugOpts_.DebugSems) {
        RootAttrs.emplace(DEBUG_SEMS_ATTR, Project->SemsDump);
    }
    return RootAttrs;
}

jinja2::ValuesMap TJinjaGenerator::FinalizeSubdirsAttrs(const std::vector<std::string>& pathPrefixes) {
    YEXPORT_VERIFY(Project, "Cannot finalize subdirs attrs because project was not yet loaded");
    jinja2::ValuesMap subdirsAttrs;
    for (const auto& subdir: Project->GetSubdirs()) {
        if (subdir->Targets.empty()) {
            continue;
        }
        if (!pathPrefixes.empty()) {
            const std::string path = subdir->Path;
            bool finalizeIt = false;
            for (const auto& prefix : pathPrefixes) {
                if (path.substr(0, prefix.size()) == prefix) {
                    finalizeIt = true;
                    break;
                }
            }
            if (!finalizeIt) {
                continue;
            }
        }
        auto [subdirIt, _] = subdirsAttrs.emplace(subdir->Path.c_str(), jinja2::ValuesMap{});
        jinja2::ValuesMap& subdirAttrs = subdirIt->second.asMap();
        std::map<std::string, std::string> semsDumps;
        for (auto target: subdir->Targets) {
            auto& targetAttrs = target->Attrs;
            targetAttrs.emplace("name", target->Name);
            targetAttrs.emplace("macro", target->Macro);
            auto isTest = target.As<TJinjaTarget>()->IsTest();
            targetAttrs.emplace("isTest", isTest);
            targetAttrs.emplace("macroArgs", jinja2::ValuesList(target->MacroArgs.begin(), target->MacroArgs.end()));
            if (!YexportSpec.AddAttrsTarget.empty()) {
                TJinjaProject::TBuilder::MergeTree(targetAttrs, YexportSpec.AddAttrsTarget);
            }

            auto targetNameAttrsIt = subdirAttrs.find(target->Macro);
            if (targetNameAttrsIt == subdirAttrs.end()) {
                jinja2::ValuesMap defaultTargetNameAttrs;
                defaultTargetNameAttrs.emplace("arcadiaRoot", ArcadiaRoot);
                if (ExportFileManager) {
                    defaultTargetNameAttrs.emplace("exportRoot", ExportFileManager->GetExportRoot());
                }
                defaultTargetNameAttrs.emplace("hasTest", false);
                defaultTargetNameAttrs.emplace("extra_targets", jinja2::ValuesList{});
                if (!YexportSpec.AddAttrsDir.empty()) {
                    TJinjaProject::TBuilder::MergeTree(defaultTargetNameAttrs, YexportSpec.AddAttrsDir);
                }
                targetNameAttrsIt = subdirAttrs.emplace(target->Macro, std::move(defaultTargetNameAttrs)).first;
                if (DumpOpts_.DumpSems || DebugOpts_.DebugSems) {
                    semsDumps.emplace(target->Macro, target->SemsDump);
                }
            }
            auto& targetNameAttrs = targetNameAttrsIt->second.asMap();
            if (isTest) {
                targetNameAttrs.insert_or_assign("hasTest", true);
            }
            if (isTest) {
                auto& extra_targets = targetNameAttrs["extra_targets"].asList();
                extra_targets.emplace_back(targetAttrs);
            } else {
                auto [targetIt, inserted] = targetNameAttrs.insert_or_assign("target", targetAttrs);
                if (!inserted) {
                    spdlog::error("Main target {} overwrote by {}", targetIt->second.asMap()["name"].asString(), targetAttrs["name"].asString());
                }
            }
        }
        if (DebugOpts_.DebugAttrs) {
            for (auto& [targetName, targetNameAttrs] : subdirAttrs) {
                targetNameAttrs.asMap().emplace(DEBUG_ATTRS_ATTR, ::NYexport::Dump(targetNameAttrs));
            }
        }
        if (DebugOpts_.DebugSems) {
            for (auto& [targetName, semsDump] : semsDumps) {
                subdirAttrs[targetName].asMap().emplace(DEBUG_SEMS_ATTR, semsDump);
            }
        }
    }
    return subdirsAttrs;
}

jinja2::ValuesMap TJinjaGenerator::FinalizeAttrsForDump() {
    // disable creating debug template attributes
    DebugOpts_.DebugSems = false;
    DebugOpts_.DebugAttrs = false;
    jinja2::ValuesMap allAttrs;
    allAttrs.emplace("root", FinalizeRootAttrs());
    allAttrs.emplace("subdirs", FinalizeSubdirsAttrs(DumpOpts_.DumpPathPrefixes));
    return allAttrs;
}

}
