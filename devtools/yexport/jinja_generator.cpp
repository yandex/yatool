#include "jinja_generator.h"
#include "read_sem_graph.h"

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
    : RootAttrs(rootAttrs)
    , Generator(generator)
{
    Project_ = MakeSimpleShared<TJinjaProject>();
}

static std::string Indent(int depth) {
    return std::string(depth * 4, ' ');
}

static void Dump(IOutputStream& out, const jinja2::Value& value, int depth = 0) {
    if (value.isMap()) {
        out << "{\n";
        const auto& map = value.asMap();
        if (!map.empty()) {
            // Sort map keys before dumping
            TVector<std::string> keys;
            keys.reserve(map.size());
            for (const auto& [key, _] : map) {
                keys.emplace_back(key);
            }
            Sort(keys);
            for (const auto& key : keys) {
                out << Indent(depth + 1) << key << ": ";
                const auto it = map.find(key);
                Dump(out, it->second, depth + 1);
            }
        }
        out << Indent(depth) << "}";
    } else if (value.isList()) {
        out << "[\n";
        for (const auto& val : value.asList()) {
            out << Indent(depth + 1);
            Dump(out, val, depth + 1);
        }
        out << Indent(depth) << "]";
    }  else if (value.isString()) {
        out << '"' << value.asString() << '"';
    } else if (value.isEmpty()) {
        out << "EMPTY";
    } else {
        out << (value.get<bool>() ? "true" : "false");
    }
    out << ",\n";
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

bool TJinjaProject::TBuilder::ValueInList(const jinja2::ValuesList& list, const jinja2::Value& val) {
    if (list.empty()) {
        return false;
    }
    TStringBuilder valStr;
    TStringBuilder listStr;
    Dump(valStr.Out, val);
    for (const auto& listVal : list) {
        listStr.clear();
        Dump(listStr.Out, listVal);
        if (valStr == listStr) {
            return true;
        }
    }
    return false;
}

bool TJinjaProject::TBuilder::AddToTargetInducedAttr(const std::string& attrMacro, const jinja2::Value& value, const std::string& nodePath) {
    if (!CurTarget_) {
        spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrMacro, nodePath);
        return false;
    }
    auto [listAttrIt, _] = CurTarget_->Attrs.emplace(attrMacro, jinja2::ValuesList{});
    auto& list = listAttrIt->second.asList();
    if (ValueInList(list, value)) {
        return true; // skip adding fully duplicate induced attributes
    }
    return AddValueToJinjaList(list, value);
}

void TJinjaProject::TBuilder::OnAttribute(const std::string& attribute) {
    Generator->OnAttribute(attribute);
}

bool TJinjaProject::TBuilder::IsExcluded(TStringBuf path, std::span<const std::string> excludes) const noexcept {
    return AnyOf(excludes.begin(), excludes.end(), [path](TStringBuf exclude) { return NPath::IsPrefixOf(exclude, path); });
};

void TJinjaProject::TBuilder::SetIsTest(bool isTestTarget) {
    CurTarget_.As<TJinjaTarget>()->isTest = isTestTarget;
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

bool TJinjaProject::TBuilder::SetStrAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    if (values.size() > 1) {
        spdlog::error("trying to add {} elements to 'str' type attribute {} at node {}, type 'str' should have only 1 element", values.size(), attrMacro, nodePath);
        r = false;
    }
    attrs.insert_or_assign(attrMacro, values.empty() ? std::string{} : values[0].asString());
    return r;
}

bool TJinjaProject::TBuilder::SetBoolAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    if (values.size() > 1) {
        spdlog::error("trying to add {} elements to 'bool' type attribute {} at node {}, type 'bool' should have only 1 element", values.size(), attrMacro, nodePath);
        r = false;
    }
    attrs.insert_or_assign(attrMacro, values.empty() ? false : IsTrue(values[0].asString()));
    return r;
}

bool TJinjaProject::TBuilder::SetFlagAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    if (values.size() > 0) {
        spdlog::error("trying to add {} elements to 'flag' type attribute {} at node {}, type 'flag' should have only 0 element", values.size(), attrMacro, nodePath);
        r = false;
    }
    attrs.insert_or_assign(attrMacro, true);
    return r;
}

bool TJinjaProject::TBuilder::AppendToListAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&) {
    auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesList{});
    auto& attr = attrIt->second.asList();
    for (const auto& value : values) {
        attr.emplace_back(value);
    }
    return true;
}

bool TJinjaProject::TBuilder::AppendToSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesList{});
    auto& attr = attrIt->second.asList();
    for (const auto& value : values) {
        if (value.isMap() || value.isList()) {
            spdlog::error("trying to add invalid type (map or list) element to set {} at {}", attrMacro, nodePath);
            r = false;
            continue;
        }
        bool exists = false;
        for (const auto& v: attr) {
            if (exists |= v.asString() == value.asString()) {
                break;
            }
        }
        if (!exists) { // add to list only if not exists
            attr.emplace_back(value);
        }
    }
    return r;
}

bool TJinjaProject::TBuilder::AppendToSortedSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesList{});
    auto& attr = attrIt->second.asList();
    std::set<std::string> set;
    if (!attr.empty()) {
        for (const auto& value : attr) {
            set.emplace(value.asString());
        }
    }
    for (const auto& value : values) {
        if (value.isMap() || value.isList()) {
            spdlog::error("trying to add invalid type (map or list) element to sorted set {} at {}", attrMacro, nodePath);
            r = false;
            continue;
        }
        set.emplace(value.asString());
    }
    attr = jinja2::ValuesList(set.begin(), set.end()); // replace by new list constructed from set
    return r;
}

bool TJinjaProject::TBuilder::AppendToDictAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
    bool r = true;
    auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesMap{});
    auto& attr = attrIt->second.asMap();
    for (const auto& value : values) {
        auto keyval = std::string_view(value.asString());
        if (auto pos = keyval.find_first_of('='); pos == std::string_view::npos) {
            spdlog::error("trying to add invalid element {} to 'dict' type attribute {} at node {}, each element must be in key=value format without spaces around =", keyval, attrMacro, nodePath);
            r = false;
        } else {
            attr.emplace(keyval.substr(0, pos), keyval.substr(pos + 1));
        }
    }
    return r;
}

void TJinjaProject::TBuilder::MergeTreeToAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesMap& tree) {
    auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesMap{});
    MergeTree(attrIt->second.asMap(), tree);
}

void TJinjaProject::TBuilder::MergeTree(jinja2::ValuesMap& attr, const jinja2::ValuesMap& tree) {
    for (auto& [attrName, attrValue]: tree) {
        if (attrValue.isMap()) {
            auto [attrIt, _] = attr.emplace(attrName, jinja2::ValuesMap{});
            MergeTree(attrIt->second.asMap(), attrValue.asMap());
        } else {
            if (attr.contains(attrName)) {
                spdlog::error("overwrite dict element {}", attrName);
            }
            attr[attrName] = attrValue;
        }
    }
}

struct TExtraStackData {
    TProject::TBuilder::TTargetHolder CurTargetHolder;
    bool FreshNode = false;
};

class TJinjaGeneratorVisitor
    : public TNoReentryVisitorBase<
          TVisitorStateItemBase,
          TSemGraphIteratorStateItem<TExtraStackData>,
          TGraphIteratorStateBase<TSemGraphIteratorStateItem<TExtraStackData>>> {
public:
    using TBase = TNoReentryVisitorBase<
        TVisitorStateItemBase,
        TSemGraphIteratorStateItem<TExtraStackData>,
        TGraphIteratorStateBase<TSemGraphIteratorStateItem<TExtraStackData>>>;
    using TState = typename TBase::TState;

    enum ESemNameType {
        ESNT_Unknown = 0,  // Semantic name not found in table
        ESNT_Target,       // Target for generator
        ESNT_RootAttr,     // Root of all targets attribute
        ESNT_TargetAttr,   // Target for generator attribute
        ESNT_InducedAttr,  // Target for generator induced attribute (add to list for parent node in graph)
        ESNT_Ignored,      // Must ignore this target for generator
    };

    TJinjaGeneratorVisitor(TJinjaGenerator* generator, jinja2::ValuesMap* rootAttrs, const TGeneratorSpec& generatorSpec)
        : ProjectBuilder(generator, rootAttrs)
    {
        for (const auto& item: generatorSpec.Targets) {
            SemName2Type_.emplace(item.first, ESNT_Target);
        }
        Attrs2SemNameType(generatorSpec, ATTRGROUP_ROOT, ESNT_RootAttr);
        Attrs2SemNameType(generatorSpec, ATTRGROUP_TARGET, ESNT_TargetAttr);
        Attrs2SemNameType(generatorSpec, ATTRGROUP_INDUCED, ESNT_InducedAttr);
        if (const auto* tests = generatorSpec.Merge.FindPtr("test")) {
            for (const auto& item: *tests) {
                TestSubdirs.push_back(item.c_str());
            }
        }
        SemName2Type_.emplace("IGNORED", ESNT_Ignored);
    }

    bool Enter(TState& state) {
        state.Top().FreshNode = TBase::Enter(state);
        if (!state.Top().FreshNode) {
            return false;
        }
        const TSemNodeData& data = state.TopNode().Value();
        if (data.Sem.empty() || data.Sem.front().empty()) {
            return true;
        }

        auto isTargetIgnored = false;
        const TNodeSemantic* targetSem = nullptr;
        for (const auto& sem: ProjectBuilder.ApplyReplacement(data.Path, data.Sem)) {
            if (sem.empty()) {
                throw yexception() << "Empty semantic item on node '" << data.Path << "'";
            }
            const auto& semName = sem[0];
            const auto semNameIt = SemName2Type_.find(semName);
            const auto semNameType = semNameIt == SemName2Type_.end() ? ESNT_Unknown : semNameIt->second;

            ProjectBuilder.OnAttribute(semName);

            if (semNameType == ESNT_Ignored) {
                isTargetIgnored = true;
            } else if (semNameType == ESNT_Target) {
                targetSem = &sem;
            } else if (semNameType == ESNT_Unknown) {
                spdlog::error("Skip unknown semantic '{}' for file '{}'", semName, data.Path);
            }
        }

        if (!isTargetIgnored && targetSem) {
            const auto& sem = *targetSem;
            const auto& semName = sem[0];
            const auto semArgs = std::span{sem}.subspan(1);
            Y_ASSERT(semArgs.size() >= 2);
            bool isTestTarget = false;
            std::string modDir = semArgs[0].c_str();
            for (const auto& testSubdir: TestSubdirs) {
                if (modDir != testSubdir && modDir.ends_with(testSubdir)) {
                    modDir.resize(modDir.size() - testSubdir.size());
                    isTestTarget = true;
                    break;
                }
            }
            auto macroArgs = semArgs.subspan(2);
            state.Top().CurTargetHolder = ProjectBuilder.CreateTarget(modDir);
            auto* curTarget = ProjectBuilder.CurrentTarget();
            curTarget->Name = semArgs[1];
            curTarget->Macro = semName;
            curTarget->MacroArgs = {macroArgs.begin(), macroArgs.end()};

            const auto* jinjaTarget = dynamic_cast<const TJinjaTarget*>(ProjectBuilder.CurrentTarget());
            ProjectBuilder.SetIsTest(isTestTarget);
            Mod2Target.emplace(state.TopNode().Id(), jinjaTarget);
        }

        return !isTargetIgnored;
    }

    void Leave(TState& state) {
        if (!state.Top().FreshNode) {
            TBase::Leave(state);
            return;
        }
        const TSemNodeData& data = state.TopNode().Value();
        if (data.Sem.empty() || data.Sem.front().empty()) {
            TBase::Leave(state);
            return;
        }

        for (const auto& sem: ProjectBuilder.ApplyReplacement(data.Path, data.Sem)) {
            const auto& semName = sem[0];
            const auto semNameIt = SemName2Type_.find(semName);
            const auto semNameType = semNameIt == SemName2Type_.end() ? ESNT_Unknown : semNameIt->second;
            const auto semArgs = std::span{sem}.subspan(1);

            if (semNameType == ESNT_Unknown || semNameType == ESNT_Target || semNameType == ESNT_Ignored) {
                // Unknown semantic error reported at Enter()
                continue;
            } else if (semNameType == ESNT_RootAttr) {
                ProjectBuilder.SetRootAttr(semName, semArgs, data.Path);
            } else if (semNameType == ESNT_TargetAttr) {
                ProjectBuilder.SetTargetAttr(semName, semArgs, data.Path);
            } else if (semNameType == ESNT_InducedAttr) {
                StoreInducedAttrValues(state.TopNode().Id(), semName, semArgs, data.Path);
            }
        }
        TBase::Leave(state);
    }

    void Left(TState& state) {
        const auto& dep = state.Top().CurDep();
        if (IsDirectPeerdirDep(dep)) {
            //Note: This part checks dependence of the test on the library in the same dir, because for java we should not distribute attributes
            bool isSameDir = false;
            const auto* toTarget = Mod2Target[dep.To().Id()];
            if (auto* curSubdir = ProjectBuilder.CurrentSubdir(); curSubdir) {
                for (const auto& target: curSubdir->Targets) {
                    if (target.Get() == toTarget) {
                        isSameDir = true;
                        break;
                    }
                }
            }
            const auto libIt = InducedAttrs_.find(dep.To().Id());
            if (!isSameDir && libIt != InducedAttrs_.end() && (!toTarget || !toTarget->isTest)) {
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
                                for (const auto& [attrMacro, value]: excludeIt->second) {
                                    auto [listIt, _] = excludes.emplace(attrMacro, jinja2::ValuesList{});
                                    AddValueToJinjaList(listIt->second.asList(), value);
                                }
                            } else {
                                spdlog::debug("Not found induced for excluded node id {} at {}", excludeNodeId, data.Path);
                            }
                        }
                    }
                }
                for (const auto& [attrMacro, value]: libIt->second) {
                    if (value.isMap() && !excludes.empty()) {
                        // For each induced attribute in map format add submap with excludes
                        jinja2::ValuesMap valueWithExcludes = value.asMap();
                        valueWithExcludes.emplace(TJinjaGenerator::EXCLUDES_ATTR, excludes);
                        ProjectBuilder.AddToTargetInducedAttr(attrMacro, valueWithExcludes, data.Path);
                    } else {
                        ProjectBuilder.AddToTargetInducedAttr(attrMacro, value, data.Path);
                    }
                }
            }
        }
        TBase::Left(state);
    }

    TProjectPtr TakeFinalizedProject() {
        return ProjectBuilder.FinishProject();
    }

private:
    THashMap<TNodeId, const TJinjaTarget*> Mod2Target;

    THashMap<TNodeId, jinja2::ValuesMap> InducedAttrs_;

    TJinjaProject::TBuilder ProjectBuilder;
    TVector<std::string> TestSubdirs;
    THashMap<std::string_view, ESemNameType> SemName2Type_;

    void Attrs2SemNameType(const TGeneratorSpec& generatorSpec, const std::string& attrGroup, ESemNameType semNameType) {
        if (const auto* attrs = generatorSpec.Attrs.FindPtr(attrGroup)) {
            TAttrsSpec const* inducedSpec = nullptr;
            if (attrGroup == ATTRGROUP_TARGET) {
                // We must skip duplicating induced attributes in target attr groups
                inducedSpec = generatorSpec.Attrs.FindPtr(ATTRGROUP_INDUCED);
            }
            for (const auto& item: attrs->Items) {
                if (inducedSpec && inducedSpec->Items.contains(item.first)) {
                    continue; // skip duplicating induced attributes in target attr group
                }
                SemName2Type_.emplace(item.first, semNameType);
            }
        }
    }

    template<IterableValues Values>
    void StoreInducedAttrValues(TNodeId nodeId, const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        auto [nodeIt, _] = InducedAttrs_.emplace(nodeId, jinja2::ValuesMap{});
        ProjectBuilder.SetInducedAttr(nodeIt->second, attrMacro, values, nodePath);
    }
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

    result->ReadYexportSpec(configDir);

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
    TJinjaGeneratorVisitor visitor(this, &JinjaAttrs, GeneratorSpec);
    IterateAll(graph, startDirs, visitor);
    Project = visitor.TakeFinalizedProject();
}

void TJinjaGenerator::LoadSemGraph(const std::string&, const fs::path& semGraph) {
    const auto [graph, startDirs] = ReadSemGraph(semGraph);
    AnalizeSemGraph(startDirs, *graph);
}

EAttrTypes TJinjaGenerator::GetAttrType(const std::string& attrGroup, const std::string& attrMacro) const {
    if (const auto* attrs = GeneratorSpec.Attrs.FindPtr(attrGroup)) {
        if (const auto it = attrs->Items.find(attrMacro); it != attrs->Items.end()) {
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
        for (auto target: subdir->Targets) {
            auto& targetAttrs = target->Attrs;
            targetAttrs.emplace("name", target->Name);
            targetAttrs.emplace("macro", target->Macro);
            targetAttrs.emplace("isTest", target.As<TJinjaTarget>()->isTest);
            targetAttrs.emplace("macroArgs", jinja2::ValuesList(target->MacroArgs.begin(), target->MacroArgs.end()));

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
            if (target.As<TJinjaTarget>()->isTest) {
                targetNameAttrs.insert_or_assign("hasTest", true);
            }
            targetNameAttrs["targets"].asList().emplace_back(targetAttrs);
        }
    }
    return subdirsAttrs;
}

}
