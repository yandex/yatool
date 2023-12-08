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

namespace NYexport {

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
        auto [listAttrIt, _] = CurTarget->JinjaAttrs.emplace(attrMacro, jinja2::ValuesList{});
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

    std::tuple<std::string, jinja2::ValuesMap> MakeTreeJinjaAttrs(const std::string& attrNameWithDividers, size_t lastDivPos, jinja2::ValuesMap&& treeJinjaAttrs) {
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
        auto attrType = Project->GetAttrType(attrGroup, attrMacro);
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

    bool AppendToListAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&) {
        auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesList{});
        auto& attr = attrIt->second.asList();
        for (const auto& value : values) {
            attr.emplace_back(value);
        }
        return true;
    }

    bool AppendToSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string&) {
        auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesList{});
        auto& attr = attrIt->second.asList();
        std::set<std::string> set;
        if (!attr.empty()) {
            for (const auto& value : attr) {
                set.emplace(value.asString());
            }
        }
        for (const auto& value : values) {
            set.emplace(value.asString());
        }
        attr = jinja2::ValuesList(set.begin(), set.end()); // replace by new list constructed from set
        return true;
    }

    bool AppendToSortedSetAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
        return AppendToSetAttr(attrs, attrMacro, values, nodePath);
    }

    bool AppendToDictAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesList& values, const std::string& nodePath) {
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

    void MergeTreeToAttr(jinja2::ValuesMap& attrs, const std::string& attrMacro, const jinja2::ValuesMap& tree) {
        auto [attrIt, _] = attrs.emplace(attrMacro, jinja2::ValuesMap{});
        MergeTree(attrIt->second.asMap(), tree);
    }

    void MergeTree(jinja2::ValuesMap& attr, const jinja2::ValuesMap& tree) {
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

            if (semName == "consumer-classpath" && !semArgs.empty()) {
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

/// Get dump of attributes tree with values for testing
void TJinjaGenerator::Dump(IOutputStream& out) {
    Dump(out, FinalizeAllAttrs());
}

void TJinjaGenerator::Render(ECleanIgnored) {
    CopyFiles();

    const auto& rootAttrs = FinalizeRootAttrs();
    auto subdirsAttrs = FinalizeSubdirsAttrs();
    for (const auto* subdirItem: SubdirsOrder) {
        const auto& subdir = subdirItem->first;
        RenderSubdir(subdir, subdirsAttrs[subdir.c_str()].asMap());
    }

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

void TJinjaGenerator::AddExcludesToTarget(const TJinjaTarget* target, jinja2::ValuesMap& targetMap, const std::string& renderPath) {
    if (targetMap.contains("consumer-classpath") && targetMap.contains("excludes_rules")) {
        targetMap.emplace("lib_excludes", jinja2::ValuesMap{});
        for (const auto& library: targetMap["consumer-classpath"].asList()) {
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
    THashMap<fs::path, TVector<TJinjaTarget>> res;
    for (const auto* subdir: SubdirsOrder) {
        TVector<TJinjaTarget> targets;
        Transform(subdir->second.Targets.begin(), subdir->second.Targets.end(), std::back_inserter(targets), [&](TJinjaTarget* target) { return *target; });
        res.emplace(subdir->first, targets);
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
    if (JinjaAttrs.contains("projectName")) { // already finilized
        return JinjaAttrs;
    }
    auto [subdirsIt, subdirsInserted] = JinjaAttrs.emplace("subdirs", jinja2::ValuesList{});
    for (const auto* subdir: SubdirsOrder) {
        subdirsIt->second.asList().emplace_back(subdir->first.c_str());
    }
    JinjaAttrs.emplace("arcadiaRoot", ArcadiaRoot);
    if (ExportFileManager) {
        JinjaAttrs.emplace("exportRoot", ExportFileManager->GetExportRoot());
    }
    JinjaAttrs.emplace("projectName", ProjectName);
    return JinjaAttrs;
}

jinja2::ValuesMap TJinjaGenerator::FinalizeSubdirsAttrs() {
    jinja2::ValuesMap subdirsAttrs;
    for (const auto* subdirItem: SubdirsOrder) {
        const auto& subdir = subdirItem->first;
        auto& subdirData = subdirItem->second;
        auto [subdirIt, _] = subdirsAttrs.emplace(subdir.c_str(), jinja2::ValuesMap{});
        jinja2::ValuesMap& subdirAttrs = subdirIt->second.asMap();
        for (auto* target: subdirData.Targets) {
            auto& targetAttrs = target->JinjaAttrs;
            targetAttrs.emplace("name", target->Name);
            targetAttrs.emplace("macro", target->Macro);
            targetAttrs.emplace("isTest", target->isTest);
            targetAttrs.emplace("macroArgs", jinja2::ValuesList(target->MacroArgs.begin(), target->MacroArgs.end()));

            AddExcludesToTarget(target, targetAttrs, (ExportFileManager ? ExportFileManager->GetExportRoot() : fs::path{}) / subdir / "*");

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
            if (target->isTest) {
                targetNameAttrs.emplace("hasTest", true);
            }
            targetNameAttrs["targets"].asList().emplace_back(targetAttrs);
        }
    }
    return subdirsAttrs;
}

static std::string Indent(int depth) {
    return std::string(depth * 4, ' ');
}

void TJinjaGenerator::Dump(IOutputStream& out, const jinja2::Value& value, int depth) {
    if (value.isMap()) {
        out << "{\n";
        for (const auto& [key, val] : value.asMap()) {
            out << Indent(depth + 1) << key << ": ";
            Dump(out, val, depth + 1);
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

}
