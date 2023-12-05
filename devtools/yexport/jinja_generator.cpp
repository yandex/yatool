#include "jinja_generator.h"
#include "read_sem_graph.h"
#include "builder.h"

#include <devtools/yexport/known_modules.h_serialized.h>

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

#include <spdlog/spdlog.h>

#include <fstream>
#include <span>

template<typename Values>
concept IterableValues = std::ranges::range<Values>;

class TJinjaGenerator::TBuilder: public TGeneratorBuilder<TSubdirsTableElem, TJinjaTarget> {
public:

    TBuilder(TJinjaGenerator* generator, TNodeId maxIdInSemGraph)
        : Project(generator)
        , LastUntrackedDependencyId(maxIdInSemGraph)
    {}

    TTargetHolder CreateTarget(const std::string& targetMacro, const fs::path& targetDir, const std::string name, std::span<const std::string> macroArgs);

    template<IterableValues Values>
    bool SetRootAttr(const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        return SetAttrValue(Project->JinjaAttrs, ATTRGROUP_ROOT, attrMacro, values, nodePath);
    }

    template<IterableValues Values>
    bool SetTargetAttr(const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        if (!CurTarget) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrMacro, nodePath);
            return false;
        }
        return SetAttrValue(CurTarget->JinjaAttrs, ATTRGROUP_TARGET, attrMacro, values, nodePath);
    }

    template<IterableValues Values>
    bool SetInducedAttr(jinja2::ValuesMap& attrs,const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        return SetAttrValue(attrs, ATTRGROUP_INDUCED, attrMacro, values, nodePath);
    }

    bool AddToTargetInducedAttr(const std::string& attrMacro, const jinja2::Value& value, const std::string& nodePath) {
        if (!CurTarget) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target at node {}", attrMacro, nodePath);
            return false;
        }
        auto [listAttrIt, inserted] = CurTarget->JinjaAttrs.emplace(attrMacro, jinja2::ValuesList{});
        if (value.isList()) {
            // Never create list of lists, if value also list, concat all lists to one big list
            for (const auto& v: value.asList()) {
                listAttrIt->second.asList().emplace_back(v);
            }
        } else {
            listAttrIt->second.asList().emplace_back(value);
        }
        return true;
    }

    void OnAttribute(const std::string& attribute) {
        Project->OnAttribute(attribute);
    }

    bool IsExcluded(TStringBuf path, std::span<const std::string> excludes) const noexcept {
        return AnyOf(excludes.begin(), excludes.end(), [path](TStringBuf exclude) { return NPath::IsPrefixOf(exclude, path); });
    };

    template<typename TVisitorState>
    void SetNodeClosure(const TVisitorState& state,
                        const std::string& nodePath,
                        const std::string& nodeCoords,
                        std::span<const std::string> peersClosure,
                        std::span<const std::string> peersClosureCoords,
                        std::span<const std::string> excludes) {
        if (peersClosure.size() != peersClosureCoords.size()) {
            spdlog::error("amount of peers closure and their coords should be equal. Current path '{}' has {} peers and {} coords", nodePath, peersClosure.size(), peersClosureCoords.size());
            return;
        }

        TNodeId nodeId = state.TopNode().Id();
        TVector<TNodeId> peersClosureIds;
        NDetail::TPeersClosure closure;
        size_t peerInd = 0;

        // Iterate over dependencies of contribs which are not in the semantic graph
        for (const auto& peerCoord: peersClosureCoords) {
            auto peerIdIt = Project->NodeIds.find(peerCoord);
            if (peerIdIt == Project->NodeIds.end()) {
                Project->NodeClosures.emplace(LastUntrackedDependencyId, NDetail::TPeersClosure{});
                Project->NodePaths.emplace(LastUntrackedDependencyId, peersClosure[peerInd]);
                Project->NodeCoords.emplace(LastUntrackedDependencyId, peerCoord);
                peerIdIt = Project->NodeIds.emplace(peerCoord, LastUntrackedDependencyId).first;
                ++LastUntrackedDependencyId;
            }

            TNodeId peerId = peerIdIt->second;
            peersClosureIds.emplace_back(peerId);
            closure.Merge(peerId, Project->NodeClosures[peerId].Exclude(
                    [&](TNodeId id) { return IsExcluded(Project->NodePaths[id], excludes); },
                    [&](TNodeId id) -> const NDetail::TPeersClosure& { return Project->NodeClosures[id]; }));

            ++peerInd;
        }

        // Iterate over dependencies from the semantic graph
        for (const auto& dep: state.TopNode().Edges()) {
            if (!IsDirectPeerdirDep(dep)) {
                continue;
            }

            TNodeId peerId = dep.To().Id();
            peersClosureIds.emplace_back(peerId);
            closure.Merge(peerId, Project->NodeClosures[peerId].Exclude(
                    [&](TNodeId id) { return IsExcluded(Project->NodePaths[id], excludes); },
                    [&](TNodeId id) -> const NDetail::TPeersClosure& { return Project->NodeClosures[id]; }));
        }

        if (!excludes.empty()) {
            for (const auto* itemStats: closure.GetStableOrderStats()) {
                if (!itemStats->second.Excluded) {
                    continue;
                }

                const auto& excludeId = itemStats->first;
                for (const auto& peerId: peersClosureIds) {
                    if (Project->NodeClosures.at(peerId).ContainsWithAnyStatus(excludeId)) {
                        CurTarget->LibExcludes[Project->NodeCoords[peerId]].emplace_back(excludeId);
                    }
                }
            }
        }

        Project->NodeClosures.emplace(nodeId, std::move(closure));
        Project->NodePaths.emplace(nodeId, nodePath);
        Project->NodeCoords.emplace(nodeId, nodeCoords);
    }

    void SetIsTest(bool isTestTarget) {
        CurTarget->isTest = isTestTarget;
    }

    const TNodeSemantics& ApplyReplacement(TPathView path, const TNodeSemantics& inputSem) const {
        return Project->ApplyReplacement(path, inputSem);
    }

    template<IterableValues Values>
    bool SetAttrValue(jinja2::ValuesMap& JinjaAttrs, const std::string& attrGroup, const std::string& attrMacro, const Values& values, const std::string& nodePath) {
        jinja2::ValuesList jvalues;
        const jinja2::ValuesList* valuesPtr;
        if constexpr(std::same_as<Values, jinja2::ValuesList>) {
            valuesPtr = &values;
        } else {
            Copy(values.begin(), values.end(), std::back_inserter(jvalues));
            valuesPtr = &jvalues;
        }
        auto attrType = Project->GetAttrType(attrGroup, attrMacro);
        Y_ASSERT(attrType != EAttrTypes::Unknown);
        switch (attrType) {
            case EAttrTypes::Str: return SetStrAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            case EAttrTypes::Bool: return SetBoolAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            case EAttrTypes::Flag: return SetFlagAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            case EAttrTypes::List: return SetListAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            case EAttrTypes::Set: return SetSetAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            case EAttrTypes::SortedSet: return SetSortedSetAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            case EAttrTypes::Dict: return SetDictAttr(JinjaAttrs, attrMacro, *valuesPtr, nodePath);
            default:{
                spdlog::error("Unknown attribute {} type at node {}", attrMacro, nodePath);
                return false;
            };
        }
    }

private:
    TJinjaGenerator* Project;
    TNodeId LastUntrackedDependencyId;

    bool SetStrAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
        bool r = true;
        if (values.size() > 1) {
            spdlog::error("trying to add {} elements to 'str' type attribute {} at node {}, type 'str' should have only 1 element", values.size(), attrMacro, nodePath);
            r = false;
        }
        attrs.insert_or_assign(attrMacro, values.empty() ? std::string{} : values[0].asString());
        return r;
    }

    bool SetBoolAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
        bool r = true;
        if (values.size() > 1) {
            spdlog::error("trying to add {} elements to 'bool' type attribute {} at node {}, type 'bool' should have only 1 element", values.size(), attrMacro, nodePath);
            r = false;
        }
        attrs.insert_or_assign(attrMacro, values.empty() ? false : IsTrue(values[0].asString()));
        return r;
    }

    bool SetFlagAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
        bool r = true;
        if (values.size() > 0) {
            spdlog::error("trying to add {} elements to 'flag' type attribute {} at node {}, type 'flag' should have only 0 element", values.size(), attrMacro, nodePath);
            r = false;
        }
        attrs.insert_or_assign(attrMacro, true);
        return r;
    }

    bool SetListAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&) {
        attrs.insert_or_assign(attrMacro, values);
        return true;
    }

    bool SetSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&) {
        std::unordered_set<std::string> set;
        for (const auto& value : values) {
            set.emplace(value.asString());
        }
        attrs.insert_or_assign(attrMacro, jinja2::ValuesList(set.begin(), set.end()));
        return true;
    }

    bool SetSortedSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&) {
        std::set<std::string> set;
        for (const auto& value: values) {
            set.emplace(value.asString());
        }
        attrs.insert_or_assign(attrMacro, jinja2::ValuesList(set.begin(), set.end()));
        return true;
    }

    bool SetDictAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
        bool r = true;
        jinja2::ValuesMap dict;
        for (const auto& value : values) {
            auto keyval = std::string_view(value.asString());
            if (auto pos = keyval.find_first_of('='); pos == std::string_view::npos) {
                spdlog::error("trying to add invalid element {} to 'dict' type attribute {} at node {}, each element must be in key=value format without spaces around =", keyval, attrMacro, nodePath);
                r = false;
            } else {
                dict.emplace(keyval.substr(0, pos), keyval.substr(pos + 1));
            }
        }
        attrs.insert_or_assign(attrMacro, std::move(dict));
        return r;
    }
};

TJinjaGenerator::TBuilder::TTargetHolder TJinjaGenerator::TBuilder::CreateTarget(const std::string& targetMacro, const fs::path& targetDir, const std::string name, std::span<const std::string> macroArgs) {
    const auto [pos, inserted] = Project->Subdirs.emplace(targetDir, TJinjaList{});
    if (inserted) {
        Project->SubdirsOrder.push_back(&*pos);
    }
    Project->Targets.push_back({.Macro = targetMacro, .Name = name, .MacroArgs = {macroArgs.begin(), macroArgs.end()}});
    pos->second.Targets.push_back(&Project->Targets.back());
    return {*this, *pos, Project->Targets.back()};
}

struct TExtraStackData {
    TJinjaGenerator::TBuilder::TTargetHolder CurTargetHolder;
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

    TJinjaGeneratorVisitor(TJinjaGenerator* generator, const TGeneratorSpec& generatorSpec, TNodeId maxIdInSemGraph)
        : ProjectBuilder(generator, maxIdInSemGraph)
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
            state.Top().CurTargetHolder = ProjectBuilder.CreateTarget(semName, modDir, semArgs[1], semArgs.subspan(2));
            ProjectBuilder.SetIsTest(isTestTarget);
            Mod2Target.emplace(state.TopNode().Id(), ProjectBuilder.CurrentTarget());
            // TODO(svidyuk) populate target props on post order part of the traversal and keep track locally used tools only instead of global dict
            TargetsDict.emplace(NPath::CutType(data.Path), ProjectBuilder.CurrentTarget());
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
            ProjectBuilder.SetNodeClosure(state, data.Path, "", {}, {}, {});
            TBase::Leave(state);
            return;
        }

        std::string nodeCoords;
        std::span<const std::string> peersClosure;
        std::span<const std::string> peersClosureCoords;
        std::span<const std::string> excludes;

        for (const auto& sem: ProjectBuilder.ApplyReplacement(data.Path, data.Sem)) {
            const auto& semName = sem[0];
            const auto semNameIt = SemName2Type_.find(semName);
            const auto semNameType = semNameIt == SemName2Type_.end() ? ESNT_Unknown : semNameIt->second;
            const auto semArgs = std::span{sem}.subspan(1);

            if (semName == "consumer_classpath" && !semArgs.empty()) {
                nodeCoords = semArgs[0];
            } else if (semName == "peers_closure") {
                peersClosure = semArgs;
            } else if (semName == "peers_closure_coords" && !semArgs.empty()) {
                peersClosureCoords = std::span{semArgs}.subspan(1);
            } else if (semName == "excludes_rules") {
                excludes = semArgs;
            }

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
        ProjectBuilder.SetNodeClosure(state, data.Path, nodeCoords, peersClosure, peersClosureCoords, excludes);
        TBase::Leave(state);
    }

    void Left(TState& state) {
        const auto& dep = state.Top().CurDep();
        if (IsDirectPeerdirDep(dep)) {
            //Note: This part checks dependence of the test on the library in the same dir, because for java we should not distribute attributes
            bool isSameDir = false;
            if (auto* curList = ProjectBuilder.CurrentList(); curList) {
                const auto toTarget = Mod2Target[dep.To().Id()];
                for (const auto target: curList->second.Targets) {
                    if (target == toTarget) {
                        isSameDir = true;
                        break;
                    }
                }
            }
            const auto libIt = InducedAttrs_.find(dep.To().Id());
            if (!isSameDir && libIt != InducedAttrs_.end()) {
                const TSemNodeData& data = dep.To().Value();
                for (const auto& [attrMacro, value]: libIt->second) {
                    ProjectBuilder.AddToTargetInducedAttr(attrMacro, value, data.Path);
                }
            }
        }
        TBase::Left(state);
    }

private:
    THashMap<TStringBuf, const TJinjaTarget*> TargetsDict;
    THashMap<TNodeId, const TJinjaTarget*> Mod2Target;

    THashMap<TNodeId, jinja2::ValuesMap> InducedAttrs_;

    TJinjaGenerator::TBuilder ProjectBuilder;
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
        auto [nodeIt, inserted] = InducedAttrs_.emplace(nodeId, jinja2::ValuesMap{});
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
    TJinjaGeneratorVisitor visitor(this, GeneratorSpec, graph.Size());
    IterateAll(graph, startDirs, visitor);
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

void TJinjaGenerator::Render(ECleanIgnored)
{
    CopyFiles();
    // Render subdir lists and collect information for root list
    auto [subdirsIt, subdirsInserted] = JinjaAttrs.emplace("subdirs", jinja2::ValuesList{});
    for (const auto* subdir: SubdirsOrder) {
        subdirsIt->second.asList().emplace_back(std::string(subdir->first.c_str()));
        RenderSubdir(subdir->first, subdir->second);
    }

    JinjaAttrs.emplace("arcadiaRoot", ArcadiaRoot);
    JinjaAttrs.emplace("exportRoot", ExportFileManager->GetExportRoot());
    JinjaAttrs.emplace("projectName", ProjectName);
    const auto& tmpls = GeneratorSpec.Root.Templates;
    for (size_t templateIndex = 0; templateIndex < tmpls.size(); templateIndex++) {
        const auto& tmpl = tmpls[templateIndex];
        //TODO: change name of result file
        jinja2::Result<TString> result = Templates[templateIndex].RenderAsString(std::as_const(JinjaAttrs));
        if (result.has_value()) {
            auto out = ExportFileManager->Open(tmpl.ResultName);
            TString renderResult = result.value();
            out.Write(renderResult.data(), renderResult.size());
        } else {
            spdlog::error("Failed to generate {} due to jinja template error: {}", tmpl.ResultName, result.error().ToString());
        }
    }
}

void TJinjaGenerator::AddExcludesToTarget(const TJinjaTarget* target, jinja2::ValuesMap& targetMap, const std::string& renderPath) {
    if (targetMap.contains("consumer_classpath") && targetMap.contains("excludes_rules")) {
        targetMap.emplace("lib_excludes", jinja2::ValuesMap{});
        for (const auto& library: targetMap["consumer_classpath"].asList()) {
            jinja2::ValuesList libraryExcludes;
            const auto& strLibrary = library.asString();

            const auto excludesIt = target->LibExcludes.find(strLibrary);
            if (excludesIt == target->LibExcludes.end()) {
                // Library has no excludes
                continue;
            }

            for (const auto& excludeId : excludesIt->second) {
                auto strExclude = NodeCoords[excludeId];
                std::erase(strExclude, '"');

                const auto groupPos = strExclude.find(':');
                if (groupPos == std::string::npos) {
                    spdlog::error("wrong exclude format '{}' can't find a group. Library {}. Problem in {}", strExclude, strLibrary, renderPath);
                    continue;
                }

                const auto modulePos = strExclude.find(':', groupPos + 1);
                if (modulePos == std::string::npos) {
                    spdlog::error("wrong exclude format '{}' can't find a module. Library {}. Problem in {}", strExclude, strLibrary, renderPath);
                    continue;
                }

                libraryExcludes.emplace_back(jinja2::ValuesList({strExclude.substr(0, groupPos), strExclude.substr(groupPos + 1, modulePos - groupPos - 1)}));
            }
            targetMap["lib_excludes"].asMap().emplace(strLibrary, libraryExcludes);
        }
    }
}

void TJinjaGenerator::RenderSubdir(const fs::path& subdir, const TJinjaList &data) {
    const auto& tmpls = GeneratorSpec.Targets.begin()->second.Templates;

    THashMap<std::string, jinja2::ValuesMap> paramsByTarget;

    for (auto* target: data.Targets) {
        auto& jinjaAttrs = target->JinjaAttrs;
        jinjaAttrs.emplace("name", target->Name);
        jinjaAttrs.emplace("macro", target->Macro);
        jinjaAttrs.emplace("isTest", target->isTest);
        jinjaAttrs.emplace("macroArgs", jinja2::ValuesList(target->MacroArgs.begin(), target->MacroArgs.end()));

        AddExcludesToTarget(target, jinjaAttrs, ExportFileManager->GetExportRoot() / subdir / "*");

        auto paramsByTargetIter = paramsByTarget.find(target->Macro);
        if (paramsByTargetIter == paramsByTarget.end()) {
            jinja2::ValuesMap defaultTargetParams;
            defaultTargetParams.emplace("arcadiaRoot", ArcadiaRoot);
            defaultTargetParams.emplace("exportRoot", ExportFileManager->GetExportRoot());
            defaultTargetParams.emplace("hasTest", false);
            defaultTargetParams.emplace("targets", jinja2::ValuesList{});
            paramsByTargetIter = paramsByTarget.emplace(target->Macro, std::move(defaultTargetParams)).first;
        }

        if (target->isTest) {
            paramsByTargetIter->second.emplace("hasTest", true);
        }
        paramsByTargetIter->second["targets"].asList().push_back(jinjaAttrs);
    }

    TemplateFs->SetRootFolder((ArcadiaRoot/subdir).string());

    for (size_t templateIndex = 0; templateIndex < tmpls.size(); templateIndex++){
        const auto& tmpl = tmpls[templateIndex];

        auto outPath = subdir / tmpl.ResultName;
        auto out = ExportFileManager->Open(outPath);

        for (auto& [targetName, params] : paramsByTarget) {
            jinja2::Result<TString> result = TargetTemplates[targetName][templateIndex].RenderAsString(std::as_const(params));
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
    THashMap<fs::path, TVector<TJinjaTarget>> res;
    for (const auto* subdir: SubdirsOrder) {
        TVector<TJinjaTarget> targets;
        Transform(subdir->second.Targets.begin(), subdir->second.Targets.end(), std::back_inserter(targets), [&](TJinjaTarget* target) { return *target; });
        res.emplace(subdir->first, targets);
    }
    return res;
}
