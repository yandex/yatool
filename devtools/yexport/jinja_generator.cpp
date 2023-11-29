#include "jinja_generator.h"
#include "fs_helpers.h"
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

#include <span>
#include <type_traits>


class TJinjaGenerator::TBuilder: public TGeneratorBuilder<TSubdirsTableElem, TJinjaTarget> {
public:

    TBuilder(TJinjaGenerator* generator, TNodeId maxIdInSemGraph)
    : Project(generator)
    , LastUntrackedDependencyId(maxIdInSemGraph)
    {}

    TTargetHolder CreateTarget(const std::string& targetMacro, const fs::path& targetDir, const std::string name, std::span<const std::string> macroArgs);

    void AddTargetAttrs(const std::string& attrMacro, const TVector<std::string>& values) {
        if (!CurTarget) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target", attrMacro);
            return;
        }
        Copy(values.begin(), values.end(), std::back_inserter(CurTarget->Attributes[attrMacro]));
    }

    void AddTargetAttrs(const std::string& attrMacro, std::span<const std::string> values) {
        if (!CurTarget) {
            spdlog::error("attempt to add target attribute '{}' while there is no active target", attrMacro);
            return;
        }
        Copy(values.begin(), values.end(), std::back_inserter(CurTarget->Attributes[attrMacro]));
    }

    void AddRootAttrs(const std::string& attrMacro, std::span<const std::string> values) {
        Copy(values.begin(), values.end(), std::back_inserter(Project->RootAttrs[attrMacro]));
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

private:
    TJinjaGenerator* Project;
    TNodeId LastUntrackedDependencyId;
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

static constexpr const TStringBuf IGNORED = "IGNORED";

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

    TJinjaGeneratorVisitor(TJinjaGenerator* generator, const TGeneratorSpec& genspec, TNodeId maxIdInSemGraph)
    : ProjectBuilder(generator, maxIdInSemGraph)
    {
        for (const auto& item: genspec.Targets) {
            KnownTargets.insert(item.first);
        }
        if (const auto* attrs = genspec.Attrs.FindPtr("target")) {
            for (const auto& item: attrs->Items) {
                KnownAttrs.insert(item.first);
            }
        }
        if (const auto* attrs = genspec.Attrs.FindPtr("induced")) {
            for (const auto& item: attrs->Items) {
                KnownInducedAttrs.insert(item.first);
            }
        }
        if (const auto* attrs = genspec.Attrs.FindPtr("root")) {
            for (const auto& item: attrs->Items) {
                KnownRootAttrs.insert(item.first);
            }
        }
        if (const auto* tests = genspec.Merge.FindPtr("test")) {
            for (const auto& item: *tests) {
                TestSubdirs.push_back(item.c_str());
            }
        }
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

        bool traverseFurther = true;
        auto isTargetIgnored = false;
        const TNodeSemantic* targetSem = nullptr;
        for (const auto& sem: ProjectBuilder.ApplyReplacement(data.Path, data.Sem)) {
            if (sem.empty()) {
                throw yexception() << "Empty semantic item on node '" << data.Path << "'";
            }
            const auto& semName = sem[0];

            ProjectBuilder.OnAttribute(semName);

            if (semName == IGNORED) {
                isTargetIgnored = true;
                traverseFurther = false;
            } else if (KnownTargets.contains(semName)) {
                targetSem = &sem;
            } else if (!(KnownAttrs.contains(semName) || KnownInducedAttrs.contains(semName) || KnownRootAttrs.contains(semName))) {
                spdlog::error("Unknown semantic '{}' for file '{}'", semName, data.Path);
                traverseFurther = false;
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

        return traverseFurther;
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

            if (semName == IGNORED || KnownTargets.contains(semName)) {
                continue;
            } else if (KnownAttrs.contains(semName)) {
                ProjectBuilder.AddTargetAttrs(semName, semArgs);
            } else if (KnownInducedAttrs.contains(semName)) {
                Copy(semArgs.begin(), semArgs.end(), std::back_inserter(InducedAttrs[state.TopNode().Id()][semName]));
            } else if (KnownRootAttrs.contains(semName)) {
                ProjectBuilder.AddRootAttrs(semName, semArgs);
            } else {
                break;
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
            const auto toTarget = Mod2Target[dep.To().Id()];
            for (const auto target: ProjectBuilder.CurrentList()->second.Targets) {
                if (target == toTarget) {
                    isSameDir = true;
                    break;
                }
            }
            const auto libIt = InducedAttrs.find(dep.To().Id());
            if (!isSameDir && libIt != InducedAttrs.end()) {
                for (const auto& [attrMacro, values]: libIt->second) {
                    ProjectBuilder.AddTargetAttrs(attrMacro, values);
                }
            }
        }
        TBase::Left(state);
    }

private:
    THashMap<TStringBuf, const TJinjaTarget*> TargetsDict;
    THashMap<TNodeId, const TJinjaTarget*> Mod2Target;

    THashMap<TNodeId, THashMap<std::string, TVector<std::string>>> InducedAttrs;

    TJinjaGenerator::TBuilder ProjectBuilder;
    THashSet<TStringBuf> KnownTargets;
    THashSet<TStringBuf> KnownAttrs;
    THashSet<TStringBuf> KnownInducedAttrs;
    THashSet<TStringBuf> KnownRootAttrs;
    TVector<std::string> TestSubdirs;
};

THolder<TJinjaGenerator> TJinjaGenerator::Load(
    const fs::path& arcadiaRoot,
    const std::string& generator,
    const fs::path& configDir
) {
    const auto generatorDir = arcadiaRoot/GENERATORS_ROOT/generator;
    const auto generatorFile = generatorDir/GENERATOR_FILE;
    if (!fs::exists(generatorFile)) {
        throw yexception() << fmt::format("[error] Failed to load generator {}. No {} file found", generator, generatorFile.c_str());
    }
    THolder<TJinjaGenerator> result = MakeHolder<TJinjaGenerator>();

    result->GeneratorSpec = ReadGeneratorSpec(generatorFile);

    result->ReadYexportSpec(configDir);

    auto setUpTemplates = [&result, &generatorDir](const std::vector<TTemplate>& sources, std::vector<jinja2::Template>& targets){
        targets.reserve(sources.size());

        for (const auto& source : sources) {
            targets.push_back(jinja2::Template(result->JinjaEnv.get()));
            targets.back().LoadFromFile(generatorDir/source.Template);
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

void TJinjaGenerator::AnalizeSemGraph(const TVector<TNodeId>& startDirs, const TSemGraph& graph)
{
    TJinjaGeneratorVisitor visitor(this, GeneratorSpec, graph.Size());
    IterateAll(graph, startDirs, visitor);
}

void TJinjaGenerator::LoadSemGraph(const std::string&, const fs::path& semGraph) {
    const auto [graph, startDirs] = ReadSemGraph(semGraph);
    AnalizeSemGraph(startDirs, *graph);
}

void TJinjaGenerator::AddStrToParams(const std::string& attrMacro, const TVector<std::string>& values, jinja2::ValuesMap& params, const std::string& renderPath) {
    if (values.size() != 1) {
        spdlog::error("trying to add to target map {} elements, type str should have only 1 element. Attribute macro: {}. Problem in {}", values.size(), attrMacro, renderPath);
    }

    if (values.empty()) {
        params.emplace(attrMacro, "");
    } else {
        params.emplace(attrMacro, values[0]);
    }
}

void TJinjaGenerator::AddBoolToParams(const std::string& attrMacro, const TVector<std::string>& values, jinja2::ValuesMap& params, const std::string& renderPath) {
    if (values.size() != 1) {
        spdlog::error("trying to add to target map {} elements, type bool should have only 1 element. Attribute macro: {}. Problem in {}", values.size(), attrMacro, renderPath);
    }

    if (values.empty()) {
        params.emplace(attrMacro, false);
    } else {
        params.emplace(attrMacro, values[0] == "true" || values[0] == "1");
    }
}

void TJinjaGenerator::AddFlagToParams(const std::string& attrMacro, const TVector<std::string>& values, jinja2::ValuesMap& params, const std::string& renderPath) {
    if (values.size() != 0) {
        spdlog::error("trying to add to target map {} elements, type flag should have only 0 elements. Attribute macro: {}. Problem in {}", values.size(), attrMacro, renderPath);
    }

    params.emplace(attrMacro, true);
}

void TJinjaGenerator::AddSortedSetToParams(const std::string& attrMacro, const TVector<std::string>& values, jinja2::ValuesMap& params) {
    auto resultValues = values;
    SortUnique(resultValues);

    params.emplace(attrMacro, jinja2::ValuesList(resultValues.begin(), resultValues.end()));
}

void TJinjaGenerator::AddSetToParams(const std::string& attrMacro, const TVector<std::string>& values, jinja2::ValuesMap& params) {
    TUniqVector<std::string> resultValues;

    for (const auto& value : values) {
        resultValues.Push(value);
    }

    params.emplace(attrMacro, jinja2::ValuesList(resultValues.begin(), resultValues.end()));
}

void TJinjaGenerator::AddListToParams(const std::string& attrMacro, const TVector<std::string>& values, jinja2::ValuesMap& params) {
    params.emplace(attrMacro, jinja2::ValuesList(values.begin(), values.end()));
}

void TJinjaGenerator::AddValuesToParams(const std::string& attrMacro, const EAttrTypes attrType, const TVector<std::string>& values, jinja2::ValuesMap& params, const std::string& renderPath) {
    switch (attrType) {
        case EAttrTypes::Str:
            AddStrToParams(attrMacro, values, params, renderPath);
            break;
        case EAttrTypes::Bool:
            AddBoolToParams(attrMacro, values, params, renderPath);
            break;
        case EAttrTypes::Flag:
            AddFlagToParams(attrMacro, values, params, renderPath);
            break;
        case EAttrTypes::List:
            AddListToParams(attrMacro, values, params);
            break;
        case EAttrTypes::Set:
            AddSetToParams(attrMacro, values, params);
            break;
        case EAttrTypes::SortedSet:
            AddSortedSetToParams(attrMacro, values, params);
            break;
        default:
            spdlog::error("undefined attribute type to add values to target map, type will be interpreted as list. Attribute macro: {}. Type: {}. Problem in {}", attrMacro, ToString(attrType), renderPath);
            AddListToParams(attrMacro, values, params);
            break;
    }
}

EAttrTypes TJinjaGenerator::GetAttrTypeFromSpec(const std::string& attrName, const std::string& attrMacro) {
    if (const auto* attrs = GeneratorSpec.Attrs.FindPtr(attrName)) {
        if (const auto it = attrs->Items.find(attrMacro); it != attrs->Items.end()) {
            return it->second.Type;
        }
    }
    return EAttrTypes::Unknown;
}

void TJinjaGenerator::Render(const fs::path& exportRoot, ECleanIgnored)
{
    CopyFiles(exportRoot);
    // Render subdir lists and collect information for root list
    for (const auto* subdir: SubdirsOrder) {
        RootAttrs["subdirs"].emplace_back(subdir->first.c_str());
        RenderSubdir(exportRoot, subdir->first, subdir->second);
    }

    jinja2::ValuesMap params;
    params.emplace("arcadiaRoot", ArcadiaRoot);
    params.emplace("exportRoot", exportRoot);
    params.emplace("projectName", ProjectName);
    for (const auto& [attrMacro, values]: RootAttrs) {
        auto attrType = attrMacro == "subdirs" ? EAttrTypes::List : GetAttrTypeFromSpec("root", attrMacro);
        if (attrType == EAttrTypes::Unknown) {
            spdlog::error("can't find type of attribute macro {} in root attrs", attrMacro);
            attrType = EAttrTypes::List;
        }

        AddValuesToParams(attrMacro, attrType, values, params, std::string(exportRoot) + "/*");
    }

    const auto& tmpls = GeneratorSpec.Root.Templates;
    for (size_t templateIndex = 0; templateIndex < tmpls.size(); templateIndex++) {
        const auto& tmpl = tmpls[templateIndex];

        //TODO: change name of result file
        TFile out = OpenOutputFile(exportRoot/tmpl.ResultName.c_str());

        TString renderResult = Templates[templateIndex].RenderAsString(std::as_const(params)).value();
        out.Write(renderResult.data(), renderResult.size());
        spdlog::info("Root {} saved", tmpl.ResultName);
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

            for (const auto& excludeId: excludesIt->second) {
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

void TJinjaGenerator::RenderSubdir(const fs::path& root, const fs::path& subdir, const TJinjaList &data) {
    const auto& tmpls = GeneratorSpec.Targets.begin()->second.Templates;

    THashMap<std::string, jinja2::ValuesMap> paramsByTarget;

    for (const auto* target: data.Targets) {
        jinja2::ValuesMap targetMap;
        targetMap.emplace("name", target->Name);
        targetMap.emplace("macro", target->Macro);
        targetMap.emplace("isTest", target->isTest);
        targetMap.emplace("macroArgs", jinja2::ValuesList(target->MacroArgs.begin(), target->MacroArgs.end()));
        for (const auto& [attrMacro, values]: target->Attributes) {
            EAttrTypes attrType = GetAttrTypeFromSpec("target", attrMacro);
            if (attrType == EAttrTypes::Unknown) {
                attrType = GetAttrTypeFromSpec("induced", attrMacro);
            }
            if (attrType == EAttrTypes::Unknown) {
                spdlog::error("can't find type of attribute macro {} in target and induced attrs", attrMacro);
                attrType = EAttrTypes::List;
            }

            AddValuesToParams(attrMacro, attrType, values, targetMap, static_cast<std::string>(root/subdir) + "/*");
        }

        AddExcludesToTarget(target, targetMap, static_cast<std::string>(root/subdir) + "/*");

        auto paramsByTargetIter = paramsByTarget.find(target->Macro);
        if (paramsByTargetIter == paramsByTarget.end()) {
            jinja2::ValuesMap defaultTargetParams;
            defaultTargetParams.emplace("arcadiaRoot", ArcadiaRoot);
            defaultTargetParams.emplace("exportRoot", root);
            defaultTargetParams.emplace("hasTest", false);
            defaultTargetParams.emplace("targets", jinja2::ValuesList{});
            paramsByTargetIter = paramsByTarget.emplace(target->Macro, std::move(defaultTargetParams)).first;
        }

        if (target->isTest) {
            paramsByTargetIter->second.emplace("hasTest", true);
        }
        paramsByTargetIter->second["targets"].asList().push_back(targetMap);
    }

    TemplateFs->SetRootFolder((ArcadiaRoot/subdir).string());

    for (size_t templateIndex = 0; templateIndex < tmpls.size(); templateIndex++){
        const auto& tmpl = tmpls[templateIndex];
        TFile out = OpenOutputFile(root/subdir/tmpl.ResultName);

        for (auto& [targetName, params]: paramsByTarget) {
            TString renderResult = TargetTemplates[targetName][templateIndex].RenderAsString(std::as_const(params)).value();
            out.Write(renderResult.data(), renderResult.size());
        }

        spdlog::info("{}/{} saved", subdir.string(), tmpl.ResultName.c_str());
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
