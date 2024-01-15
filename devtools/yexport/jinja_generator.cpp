#include "jinja_generator.h"
#include "read_sem_graph.h"
#include "graph_visitor.h"

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

std::tuple<std::string, jinja2::ValuesMap> TJinjaProject::TBuilder::MakeTreeJinjaAttrs(const std::string& attrNameWithDividers, size_t lastDivPos, jinja2::ValuesMap&& treeJinjaAttrs) {
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

bool TJinjaProject::TBuilder::SetStrAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    if (values.size() > 1) {
        spdlog::error("trying to add {} elements to 'str' type attribute {} at node {}, type 'str' should have only 1 element", values.size(), attrName, nodePath);
        r = false;
    }
    bool inserted = attrs.insert_or_assign(attrName, values.empty() ? std::string{} : values[0].asString()).second;
    if (!inserted) {
        spdlog::error("trying to set string value of attribute {} at node {}, but it already has value. Attribute value will be overwritten", attrName, nodePath);
    };
    return r;
}

bool TJinjaProject::TBuilder::SetBoolAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    if (values.size() > 1) {
        spdlog::error("trying to add {} elements to 'bool' type attribute {} at node {}, type 'bool' should have only 1 element", values.size(), attrName, nodePath);
        r = false;
    }
    bool inserted = attrs.insert_or_assign(attrName, values.empty() ? false : IsTrue(values[0].asString())).second;
    if (!inserted) {
        spdlog::error("trying to set bool value of attribute {} at node {}, but it already has value. Attribute value will be overwritten", attrName, nodePath);
    }
    return r;
}

bool TJinjaProject::TBuilder::SetFlagAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    if (values.size() > 0) {
        spdlog::error("trying to add {} elements to 'flag' type attribute {} at node {}, type 'flag' should have only 0 element", values.size(), attrName, nodePath);
        r = false;
    }
    attrs.insert_or_assign(attrName, true);
    return r;
}

jinja2::Value TJinjaProject::TBuilder::GetItemValue(const std::string& attrGroup, const std::string& attrName, const jinja2::Value& value, const std::string& nodePath) {
    jinja2::ValuesMap tempAttrs;
    auto attrNameItem = attrName + ITEM_TYPE;
    SetAttrValue(tempAttrs, attrGroup, attrNameItem, jinja2::ValuesList{value}, nodePath, true);
    return tempAttrs[attrNameItem];
}

bool TJinjaProject::TBuilder::AppendToListAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
    auto& attr = attrIt->second.asList();
    for (const auto& value : values) {
        attr.emplace_back(GetItemValue(attrGroup, attrName, value, nodePath));
    }
    return true;
}

bool TJinjaProject::TBuilder::AppendToSetAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
    auto& attr = attrIt->second.asList();
    for (const auto& value : values) {
        if (value.isMap() || value.isList()) {
            spdlog::error("trying to add invalid type (map or list) element to set {} at {}", attrName, nodePath);
            r = false;
            continue;
        }
        auto itemValue = GetItemValue(attrGroup, attrName, value, nodePath);
        bool exists = false;
        for (const auto& v: attr) {
            if (exists |= v.asString() == itemValue.asString()) {
                break;
            }
        }
        if (!exists) { // add to list only if not exists
            attr.emplace_back(itemValue);
        }
    }
    return r;
}

bool TJinjaProject::TBuilder::AppendToSortedSetAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesList{});
    auto& attr = attrIt->second.asList();
    std::set<std::string> set;
    if (!attr.empty()) {
        for (const auto& value : attr) {
            set.emplace(value.asString());
        }
    }
    for (const auto& value : values) {
        if (value.isMap() || value.isList()) {
            spdlog::error("trying to add invalid type (map or list) element to sorted set {} at {}", attrName, nodePath);
            r = false;
            continue;
        }
        set.emplace(GetItemValue(attrGroup, attrName, value, nodePath).asString());
    }
    return r;
}

bool TJinjaProject::TBuilder::AppendToDictAttr(jinja2::ValuesMap& attrs, const std::string& attrGroup, const std::string& attrName, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesMap{});
    auto& attr = attrIt->second.asMap();
    for (const auto& value : values) {
        auto keyval = std::string_view(value.asString());
        if (auto pos = keyval.find_first_of('='); pos == std::string_view::npos) {
            spdlog::error("trying to add invalid element {} to 'dict' type attribute {} at node {}, each element must be in key=value format without spaces around =", keyval, attrName, nodePath);
            r = false;
        } else {
            attr.emplace(keyval.substr(0, pos), GetItemValue(attrGroup, attrName, jinja2::Value{keyval.substr(pos + 1)}, nodePath));
        }
    }
    return r;
}

void TJinjaProject::TBuilder::MergeTreeToAttr(jinja2::ValuesMap& attrs, const std::string& attrName, const jinja2::ValuesMap& tree) {
    auto [attrIt, _] = attrs.emplace(attrName, jinja2::ValuesMap{});
    MergeTree(attrIt->second.asMap(), tree);
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
                        Y_ASSERT(excludesIt->second.isList());
                        // for each excluded node id get all it induced attrs
                        for (const auto& excludeNodeVal : excludesIt->second.asList()) {
                            const auto excludeNodeId = static_cast<TNodeId>(excludeNodeVal.get<int64_t>());
                            const auto excludeIt = InducedAttrs_.find(excludeNodeId);
                            if (excludeIt != InducedAttrs_.end()) {
                                // Put all induced attrs of excluded library to lists by induced attribute name
                                for (const auto& [attrName, value]: excludeIt->second) {
                                    auto [listIt, _] = excludes.emplace(attrName, jinja2::ValuesList{});
                                    AddValueToJinjaList(listIt->second.asList(), value);
                                }
                            } else {
                                spdlog::debug("Not found induced for excluded node id {} at {}", excludeNodeId, data.Path);
                            }
                        }
                    }
                }
                const auto fromTarget = Mod2Target_[dep.From().Id()];
                bool test2test = fromTarget && fromTarget->IsTest() && toTarget && toTarget->IsTest();
                for (const auto& [attrName, value]: libIt->second) {
                    if (value.isMap() && (!excludes.empty() || test2test)) {
                        // For each induced attribute in map format add submap with excludes
                        jinja2::ValuesMap valueWithDepAttrs = value.asMap();
                        if (!excludes.empty()) {
                            valueWithDepAttrs.emplace(TJinjaGenerator::EXCLUDES_ATTR, excludes);
                        }
                        if (test2test) {
                            valueWithDepAttrs.emplace(TJinjaGenerator::TEST2TEST_ATTR, toTarget->TestModDir);
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
    const fs::path& configDir
) {
    const auto generatorDir = arcadiaRoot/GENERATORS_ROOT/generator;
    const auto generatorFile = generatorDir/GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        YEXPORT_THROW(fmt::format("Failed to load generator {}. No {} file found", generator, generatorFile.c_str()));
    }
    THolder<TJinjaGenerator> result = MakeHolder<TJinjaGenerator>();

    result->GeneratorSpec = ReadGeneratorSpec(generatorFile);
    result->YexportSpec = result->ReadYexportSpec(configDir);

    auto setUpTemplates = [&result, &generatorDir](const std::vector<TTemplate>& sources, std::vector<jinja2::Template>& targets){
        targets.reserve(sources.size());

        for (const auto& source : sources) {
            targets.push_back(jinja2::Template(result->JinjaEnv.get()));

            auto path = generatorDir / source.Template;
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

    setUpTemplates(result->GeneratorSpec.Root.Templates, result->Templates);

    result->ArcadiaRoot = arcadiaRoot;
    result->JinjaEnv->AddFilesystemHandler({}, result->TemplateFs);
    result->JinjaEnv->GetSettings().cacheSize = 0;
    result->JinjaEnv->AddGlobal("split", jinja2::UserCallable{
        .callable = [](const jinja2::UserCallableParams& params) -> jinja2::Value {
            auto str = params["str"].asString();
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
        .argsInfo = { jinja2::ArgInfo{"str"}, jinja2::ArgInfo{"delimeter", false, " "} }
    });
    result->GeneratorDir = generatorDir;

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
    TNewJinjaGeneratorVisitor visitor(this, &JinjaAttrs, GeneratorSpec);
    IterateAll(graph, startDirs, visitor);
    Project = visitor.TakeFinalizedProject();
}

void TJinjaGenerator::LoadSemGraph(const std::string&, const fs::path& semGraph) {
    const auto [graph, startDirs] = ReadSemGraph(semGraph, GeneratorSpec.UseManagedPeersClosure);
    AnalizeSemGraph(startDirs, *graph);
}

EAttrTypes TJinjaGenerator::GetAttrType(const std::string& attrGroup, const std::string& attrName) const {
    if (const auto* attrs = GeneratorSpec.Attrs.FindPtr(attrGroup)) {
        if (const auto it = attrs->Items.find(attrName); it != attrs->Items.end()) {
            return it->second.Type;
        }
    }
    return EAttrTypes::Unknown;
}

/// Get dump of attributes tree with values for testing
void TJinjaGenerator::Dump(IOutputStream& out) {
    ::NYexport::Dump(out, FinalizeAllAttrs());
}

void TJinjaGenerator::Render(ECleanIgnored) {
    YEXPORT_VERIFY(Project, "Cannot render because project was not yet loaded");
    CopyFilesAndResources();
    DoDump = true;
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
    TemplateFs->SetRootFolder((ArcadiaRoot/subdir).string());
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

jinja2::ValuesMap TJinjaGenerator::FinalizeAllAttrs() {
    jinja2::ValuesMap allAttrs;
    DoDump = false;
    allAttrs.emplace("root", FinalizeRootAttrs());
    allAttrs.emplace("subdirs", FinalizeSubdirsAttrs());
    return allAttrs;
}

const jinja2::ValuesMap& TJinjaGenerator::FinalizeRootAttrs() {
    YEXPORT_VERIFY(Project, "Cannot finalize root attrs because project was not yet loaded");
    if (JinjaAttrs.contains("projectName")) { // already finilized
        return JinjaAttrs;
    }
    auto [subdirsIt, subdirsInserted] = JinjaAttrs.emplace("subdirs", jinja2::ValuesList{});
    for (const auto& subdir: Project->GetSubdirs()) {
        if (subdir->Targets.empty()) {
            continue;
        }
        subdirsIt->second.asList().emplace_back(subdir->Path.c_str());
    }
    JinjaAttrs.emplace("arcadiaRoot", ArcadiaRoot);
    if (ExportFileManager) {
        JinjaAttrs.emplace("exportRoot", ExportFileManager->GetExportRoot());
    }
    JinjaAttrs.emplace("projectName", ProjectName);
    if (!YexportSpec.AddAttrsDir.empty()) {
        JinjaAttrs.emplace("add_attrs", YexportSpec.AddAttrsDir);
    }
    if (DoDump) {
        JinjaAttrs.emplace("dump", ::NYexport::Dump(JinjaAttrs, 0, "// "));
    }
    return JinjaAttrs;
}

jinja2::ValuesMap TJinjaGenerator::FinalizeSubdirsAttrs() {
    YEXPORT_VERIFY(Project, "Cannot finalize subdirs attrs because project was not yet loaded");
    jinja2::ValuesMap subdirsAttrs;
    for (const auto& subdir: Project->GetSubdirs()) {
        if (subdir->Targets.empty()) {
            continue;
        }
        auto [subdirIt, _] = subdirsAttrs.emplace(subdir->Path.c_str(), jinja2::ValuesMap{});
        jinja2::ValuesMap& subdirAttrs = subdirIt->second.asMap();
        if (!YexportSpec.AddAttrsDir.empty()) {
            for (const auto& [k, v]: YexportSpec.AddAttrsDir) {
                subdirAttrs.emplace(k, v);
            }
        }
        for (auto target: subdir->Targets) {
            auto& targetAttrs = target->Attrs;
            targetAttrs.emplace("name", target->Name);
            targetAttrs.emplace("macro", target->Macro);
            auto isTest = target.As<TJinjaTarget>()->IsTest();
            targetAttrs.emplace("isTest", isTest);
            targetAttrs.emplace("macroArgs", jinja2::ValuesList(target->MacroArgs.begin(), target->MacroArgs.end()));
            if (!YexportSpec.AddAttrsTarget.empty()) {
                for (const auto& [k, v]: YexportSpec.AddAttrsTarget) {
                    targetAttrs.emplace(k, v);
                }
            }

            auto targetNameAttrsIt = subdirAttrs.find(target->Macro);
            if (targetNameAttrsIt == subdirAttrs.end()) {
                jinja2::ValuesMap defaultTargetParams;
                defaultTargetParams.emplace("arcadiaRoot", ArcadiaRoot);
                if (ExportFileManager) {
                    defaultTargetParams.emplace("exportRoot", ExportFileManager->GetExportRoot());
                }
                defaultTargetParams.emplace("hasTest", false);
                defaultTargetParams.emplace("targets", jinja2::ValuesList{});
                targetNameAttrsIt = subdirAttrs.emplace(target->Macro, std::move(defaultTargetParams)).first;
            }
            auto& targetNameAttrs = targetNameAttrsIt->second.asMap();
            if (isTest) {
                targetNameAttrs.insert_or_assign("hasTest", true);
            }
            auto& targets = targetNameAttrs["targets"].asList();
            if (isTest || targets.empty()) {
                targets.emplace_back(targetAttrs);
            } else { // non-test targets always put to begin of targets
                targets.insert(targets.begin(), targetAttrs);
            }
        }
        if (DoDump) {
            for (auto& [targetName, targetNameAttrs] : subdirAttrs) {
                targetNameAttrs.asMap().emplace("dump", ::NYexport::Dump(targetNameAttrs, 0, "// "));
            }
        }
    }
    return subdirsAttrs;
}

}
