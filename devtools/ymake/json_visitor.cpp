#include "json_visitor.h"

#include "vars.h"
#include "module_restorer.h"
#include "prop_names.h"
#include "json_saveload.h"

#include <devtools/ymake/symbols/symbols.h>

#include <devtools/ymake/common/md5sig.h>
#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/compact_graph/loops.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/dependency_management.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/parser_manager.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/reserve.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>
#include <util/stream/str.h>
#include <util/string/split.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

#include <utility>

namespace {
    EShowExpressionErrors ParseErrorMode(TStringBuf mode) {
        return
            mode == "all" ? EShowExpressionErrors::All :
            mode == "one" ? EShowExpressionErrors::One :
            EShowExpressionErrors::None;
    }
}

inline bool NeedToPassInputs(const TConstDepRef& dep) {
    if (dep.To()->NodeType == EMNT_Program || dep.To()->NodeType == EMNT_Library) {
        return false;
    }

    return true;
}

TJSONVisitor::TJSONVisitor(const TRestoreContext& restoreContext, TCommands& commands, const TCmdConf& cmdConf, const TVector<TTarget>& startDirs)
    : TBase{restoreContext, commands, cmdConf, startDirs}
    , GlobalVarsCollector(restoreContext)
    , JsonDepsFromMainOutputEnabled_(restoreContext.Conf.JsonDepsFromMainOutputEnabled())
    , ErrorShower(ParseErrorMode(restoreContext.Conf.ExpressionErrorDetails))
{
    if (JsonDepsFromMainOutputEnabled_) {
        YDebug() << "Passing JSON dependencies from main to additional outputs enabled" << Endl;
    }

    LoopCnt.resize(Loops.size());

    for (TTarget target : startDirs) {
        if (target.IsModuleTarget) {
            StartModules.insert(target);
        }
    }
}

void TJSONVisitor::SaveLoop(TSaveBuffer* buffer, TNodeId loopId, const TDepGraph& graph) {
    const TGraphLoop& loop = Loops[loopId];

    buffer->Save(LoopCnt[loopId].SelfSign.GetRawData(), 16);
    buffer->Save(LoopCnt[loopId].Sign.GetRawData(), 16);
    buffer->Save<ui32>(loop.Deps.size());
    for (TNodeId depNode : loop.Deps) {
        buffer->SaveElemId(depNode, graph);
    }
}

bool TJSONVisitor::LoadLoop(TLoadBuffer* buffer, TNodeId nodeFromLoop, const TDepGraph& graph) {
    TNodeId* loopId = Loops.Node2Loop.FindPtr(nodeFromLoop);
    if (!loopId)
        return false;

    buffer->LoadMd5(&LoopCnt[*loopId].SelfSign);
    buffer->LoadMd5(&LoopCnt[*loopId].Sign);

    ui32 depsCount = buffer->Load<ui32>();

    TGraphLoop& loop = Loops[*loopId];
    loop.Deps.reserve(depsCount);
    for (size_t i = 0; i < depsCount; ++i) {
        TNodeId depNode;
        if (!buffer->LoadElemId(&depNode, graph)) {
            loop.Deps.clear();
            return false;
        }
        loop.Deps.push_back(depNode);
    }

    loop.DepsDone = true;
    return true;
}

void TJSONVisitor::SaveCache(IOutputStream* output, const TDepGraph& graph) {
    TVector<ui8> rawBuffer;
    rawBuffer.reserve(64 * 1024);

    ui32 nodesCount = 0;
    for (const auto& [nodeId, nodeData] : Nodes) {
        if (nodeData.Completed) {
            ++nodesCount;
        }
    }
    output->Write(&nodesCount, sizeof(nodesCount));

    for (const auto& [nodeId, nodeData] : Nodes) {
        if (!nodeData.Completed) {
            continue;
        }
        TSaveBuffer buffer{&rawBuffer};
        nodeData.Save(&buffer, graph);
        buffer.SaveNodeDataToStream(output, nodeId, graph);
    }
    CacheStats.Set(NStats::EUidsCacheStats::SavedNodes, nodesCount);

    ui32 loopsCount = Loops.empty() ? 0 : Loops.size() - 1;
    output->Write(&loopsCount, sizeof(loopsCount));
    for (size_t loopId = 1; loopId <= loopsCount; ++loopId) {
        TSaveBuffer buffer{&rawBuffer};
        SaveLoop(&buffer, loopId, graph);

        // Идентифицируем цикл по любому из его узлов.
        // Если цикл не изменился, то подойдёт любой узел.
        // Если цикл изменился, каждый его узел изменится,
        // и мы не будем загружать его данные из кэша.
        buffer.SaveNodeDataToStream(output, Loops[loopId][0], graph);
    }

    CacheStats.Set(NStats::EUidsCacheStats::SavedLoops, loopsCount);
}

void TJSONVisitor::LoadCache(IInputStream* input, const TDepGraph& graph) {
    TVector<ui8> rawBuffer;
    rawBuffer.reserve(64 * 1024);

    ui32 nodesCount = LoadFromStream<ui32>(input);

    for (size_t i = 0; i < nodesCount; ++i) {
        TLoadBuffer buffer{&rawBuffer};
        TNodeId nodeId;
        auto nodeLoaded = buffer.LoadUnchangedNodeDataFromStream(input, nodeId, graph, sizeof(TMd5Sig));

        auto nodeRef = graph.Get(nodeId);
        auto [it, added] = Nodes.try_emplace(nodeRef.Id(), TJSONEntryStats::TItemDebug{graph, nodeRef.Id()});
        auto& [_, nodeData] = *it;
        nodeData.InStack = false;
        Y_ASSERT(added);

        if (!nodeLoaded) {
            nodeData.LoadStructureUid(&buffer, graph, true);
            CacheStats.Inc(NStats::EUidsCacheStats::SkippedNodes);
            continue;
        }

        if (!nodeData.Load(&buffer, graph)) {
            CacheStats.Inc(NStats::EUidsCacheStats::DiscardedNodes);
            Nodes.erase(nodeRef.Id());
        }

        CacheStats.Inc(NStats::EUidsCacheStats::LoadedNodes);
    }

    ui32 loopsCount = LoadFromStream<ui32>(input);

    for (size_t i = 0; i < loopsCount; ++i) {
        TLoadBuffer buffer{&rawBuffer};
        TNodeId loopNodeId;
        if (!buffer.LoadUnchangedNodeDataFromStream(input, loopNodeId, graph)) {
            CacheStats.Inc(NStats::EUidsCacheStats::SkippedLoops);
            continue;
        }

        if (!LoadLoop(&buffer, loopNodeId, graph)) {
            CacheStats.Inc(NStats::EUidsCacheStats::DiscardedLoops);
        } else {
            CacheStats.Inc(NStats::EUidsCacheStats::LoadedLoops);
        }
    }
}

THashMap<TString, TMd5Sig> TJSONVisitor::GetInputs(const TDepGraph& graph) const {
    THashMap<TString, TMd5Sig> result;
    for (const auto& input : Inputs) {
        result[graph.GetFileName(input.first).GetTargetStr()] = input.second;
    }
    return result;

}
const THashMap<TString, TString>& TJSONVisitor::GetResources() const {
    return Resources;
}

const TVector<TString>& TJSONVisitor::GetHostResources() const {
    return HostResources;
}

TNodeId TJSONVisitor::GetModuleByNode(TNodeId nodeId) {
    return Node2Module[nodeId];
}

TSimpleSharedPtr<TUniqVector<TNodeId>>& TJSONVisitor::GetNodeInputs(TNodeId node) {
    if (const auto it = Loops.Node2Loop.find(node)) {
        const auto loopId = it->second;
        return LoopsInputs[loopId];
    } else {
        return NodesInputs[node];
    }
}

// Graph traversal rules:
// 1. Do not re-subscan Include dirs (i.e. dirs entered through non-BuildFrom edge).
// 2. Do not re-subscan built file nodes.
// 3. Always re-subscan all other nodes, unless they have no built nodes up ahead.
bool TJSONVisitor::Enter(TState& state) {
    bool fresh = TBase::Enter(state);
    TStateItem& currState = state.Top();
    const auto node = currState.Node();
    const auto nodeType = node->NodeType;
    const auto& graph = TDepGraph::Graph(node);
    TJSONEntryStats& currData = *CurEnt;

    currData.WasVisited = true;

    TStateItem* prntState = nullptr;
    TJSONEntryStats* prntData = nullptr;
    if (state.HasIncomingDep()) {
        prntState = &*state.Parent();
        prntData = VisitorEntry(*prntState);
    }

    if (nodeType == EMNT_BuildCommand && state.HasIncomingDep() && *state.IncomingDep() == EDT_BuildCommand) {
        if (currState.GetCmdName().IsNewFormat()) {
            prntData->StructCmdDetected = true;
        }
    }

    if (fresh) {
        TNodeDebugOnly nodeDebug{graph, node.Id()};

        if (nodeType == EMNT_File || nodeType == EMNT_MakeFile) {
            auto elemId = currState.Node()->ElemId;
            const TFileData& fileData = graph.Names().FileConf.GetFileDataById(elemId);
            Y_ASSERT(fileData.HashSum != TMd5Sig());
            // TODO: Inputs нужны только если включен Conf.DumpInputsMapInJSON
            // Если это редкий режим, вынести под условие.
            Inputs.push_back(std::make_pair(elemId, fileData.HashSum));
        }

        if (state.HasIncomingDep() && *state.IncomingDep() == EDT_BuildFrom && !IsModuleType(nodeType)) {
            if (prntData->Fake) {
                // Module with Fake attribute may be a program and thus not fake itself, but its own BFs are still fake
                currData.Fake = true;
                YDIAG(V) << "Node marked as fake: " << currState.Print() << " borrowed from: " << prntState->Print() << Endl;
            }
        }

        if (const auto it = Loops.Node2Loop.find(node.Id())) {
            currData.LoopId = it->second;
        }

        if (IsModule(currState)) {
            currData.IsGlobalVarsCollectorStarted = GlobalVarsCollector.Start(currState);
        }
    }

    // TODO: Этот код выполняется при каждом Enter, а возможно нужно только при первом.
    if (RestoreContext.Conf.DumpInputsInJSON && nodeType == EMNT_File) {
        AddTo(node.Id(), GetNodeInputs(node.Id()));
    }

    currData.WasFresh = fresh;
    YDIAG(Dev) << "JSON: Enter " << currState.Print() << ". Freshness = " << fresh << "; LoopId = " << currData.LoopId << Endl;

    if (nodeType == EMNT_Directory && (!state.HasIncomingDep() || *state.IncomingDep() != EDT_BuildFrom)) {
        return fresh;
    }

    if (fresh) {
        if (IsModuleType(nodeType)) {
            currState.Module = RestoreContext.Modules.Get(node->ElemId);
            Y_ENSURE(currState.Module != nullptr);
            currData.Fake = currState.Module->IsFakeModule();
        }

        if (const auto it = Loops.Node2Loop.find(node.Id())) {
            currData.LoopId = it->second;
        }
    }

    return fresh;
}

void TJSONVisitor::PrepareLeaving(TState& state) {
    const TStateItem& currState = state.Top();
    const auto currNode = currState.Node();
    const auto& graph = TDepGraph::Graph(currNode);
    TJSONEntryStats& currData = *CurEnt;
    bool currDone = currData.Completed;
    bool prntDone = false;

    TStateItem* prntState = nullptr;
    TJSONEntryStats* prntData = nullptr;
    if (state.HasIncomingDep()) {
        prntState = &*state.Parent();
        prntData = VisitorEntry(*prntState);
        prntDone = prntData->Completed;
    }

    if (IsModule(currState) && std::exchange(currData.IsGlobalVarsCollectorStarted, false)) {
        GlobalVarsCollector.Finish(currState, &currData);
    }

    // Note that the following code should be unified with Left()
    if (state.HasIncomingDep()) {
        const auto prntNode = prntState->Node();
        const auto incDep = prntState->CurDep();

        const bool isParentBuildEntry = prntData->IsFile && (*incDep == EDT_BuildFrom || *incDep == EDT_BuildCommand);
        auto& prntDestSet = isParentBuildEntry ? prntData->NodeDeps : prntData->IncludedDeps;
        TNodeId tool = 0;
        bool bundle = false;

        YDIAG(Dev) << "JSON: PrepareLeaving " << currState.Print() << "; isParentBuildEntry = " << isParentBuildEntry << Endl;
        if (currData.IsFile && (currData.HasBuildCmd || currData.OutTogetherDependency != 0)) {
            if (IsModuleType(currNode->NodeType) && IsDirectToolDep(incDep)) {
                auto name = prntState->GetCmdName();
                tool = currNode.Id();
                bundle = graph.Names().CommandConf.GetById(name.GetCmdId()).KeepTargetPlatform;
            } else if (!currData.Fake) {
                YDIAG(Dev) << "JSON: PrepareLeaving " << currState.Print() << "; add to " << (isParentBuildEntry ? "cmd" : "include") << " deps for " << prntState->Print() << Endl;
                if (!prntDone)
                    prntDestSet.Add(currNode.Id());
                if (!currDone)
                    currData.IncludedDeps.Add(currNode.Id());
            }

            const TNodeId outTogetherDependencyId = currData.OutTogetherDependency;
            if (auto [node2ModuleIt, node2ModuleAdded] = Node2Module.try_emplace(currNode.Id(), 0); node2ModuleAdded) {
                const auto moduleState = FindModule(state); // we set it on first leaving: module should be in stack
                const bool hasModule = moduleState != state.end();
                TJSONEntryStats* moduleData = hasModule ? VisitorEntry(*moduleState) : nullptr;
                bool moduleDone = moduleData ? moduleData->Completed : false;
                if (hasModule) {
                    node2ModuleIt->second = moduleState->Node().Id();
                }

                bool addToOuts = hasModule && moduleState->Module->IsExtraOut(currNode->ElemId);
                if (!moduleDone && NeedAddToOuts(state, *currNode) && !currData.Fake) {
                    AddTo(currNode.Id(), moduleData->ExtraOuts);
                    if (outTogetherDependencyId) {
                        moduleData->NodeDeps.Add(outTogetherDependencyId);
                    } else if (addToOuts) {
                        moduleData->NodeDeps.Add(currNode.Id());
                    }
                }
            }

            if (!prntDone && outTogetherDependencyId && !currData.Fake) {
                YDIAG(Dev) << "JSON: PrepareLeaving " << currState.Print() << "; as AlsoBuilt add to " << (isParentBuildEntry ? "cmd" : "include") << " deps for " << prntState->Print() << Endl;
                prntDestSet.Add(outTogetherDependencyId);
            }
        }

        if (tool) {
            if (!prntDone) {
                if (!bundle) {
                    prntData->NodeToolDeps.Add(tool);
                }
                prntDestSet.Add(tool);  // Note: this ensures tools in regular deps
            }
        } else {
            if (!prntDone) {
                if (!JsonDepsFromMainOutputEnabled_) {
                    if (state.Parent()->CurDep().Value() != EDT_OutTogether) {
                        prntDestSet.Add(currData.IncludedDeps);
                    } else {
                        prntDestSet.Add(currNode.Id());
                    }
                } else {
                    prntDestSet.Add(currData.IncludedDeps);
                }
                if (IsDirectPeerdirDep(incDep) && prntState->Module->PassPeers()) {
                    prntData->IncludedDeps.Add(currData.IncludedDeps);
                }
            }
        }

        if (IsModule(currState) && currData.WasFresh) {
            const auto mod = currState.Module;
            if (mod->IsDependencyManagementApplied()) {
                TUniqVector<TNodeId>* oldDeps = currData.NodeDeps.Get();

                // Here we recalculate NodeDeps for nodes under dependency management.
                // First of all we should take all "managed peers"
                // calculated by dependency management, as they
                // 1) have filtered set of dependencies: only selected versions and
                // with excludes taken into the account.
                // 2) can have additional dependencies such as non-managed dependencies
                // from transitive dependencies closure.

                TUniqVector<TNodeId> deps;
                if (mod->GetPeerdirType() == EPT_BuildFrom) {
                    const auto& lists = RestoreContext.Modules.GetNodeListStore();
                    const auto& ids = RestoreContext.Modules.GetModuleNodeIds(mod->GetId());
                    const auto& managedPeers = ids.UniqPeers;
                    for (auto peerId : lists.GetList(managedPeers)) {
                        auto peerModule = RestoreContext.Modules.Get(graph.Get(peerId)->ElemId);
                        if (peerModule->IsFakeModule()) {
                            continue;
                        }
                        deps.Push(peerId);
                    }
                }

                // Secondly we should take all non-module nodes from the regular NodeDeps.
                // There are things such as BuildCommand nodes for example.
                // We also should skip all modules, as they have all non-filtered
                // dependencies. And all really used modules already was in "managed peers".

                if (oldDeps) {
                    for (auto depId : *oldDeps) {
                        if (IsModuleType(graph.Get(depId)->NodeType)) {
                            continue;
                        }
                        deps.Push(depId);
                    }
                }

                // Then take NodeToolDeps as it is. No specific treatment needed.

                if (currData.NodeToolDeps) {
                    for (auto toolDep : *currData.NodeToolDeps) {
                        deps.Push(toolDep);
                    }
                }

                // Finally set new NodeDeps value, minding to leave NodeDeps holder
                // reseted when there are no values. There are supposedly some code
                // dependent on that same behaviour and I have no wish to search
                // and clean up all such places.

                if (!deps.empty()) {
                    if (!oldDeps) {
                        currData.NodeDeps.Reset(new TUniqVector<TNodeId>{});
                        oldDeps = currData.NodeDeps.Get();
                    }

                    oldDeps->swap(deps);

                } else {
                    currData.NodeDeps.Reset();
                }
            }
        }


        if (!prntDone && IsInnerCommandDep(incDep) || IsBuildCommandDep(incDep)) {
            prntData->NodeToolDeps.Add(currData.NodeToolDeps);
        }

        if (!tool && RestoreContext.Conf.DumpInputsInJSON && NeedToPassInputs(incDep)) {
            AddTo(GetNodeInputs(currNode.Id()), GetNodeInputs(prntNode.Id()));
        }
    }

    if (state.HasIncomingDep() && (
        IsInnerCommandDep(state.IncomingDep()) ||
        IsBuildCommandDep(state.IncomingDep()) ||
        IsLocalVariableDep(state.IncomingDep())
    )) {
        const auto usedVars = currData.UsedReservedVars.Get();
        if (usedVars) {
            if (!prntDone) {
                GetOrInit(prntData->UsedReservedVars).insert(usedVars->begin(), usedVars->end());
            }
        }
    }

    if (currData.WasFresh) {
        currData.WasFresh = false;

        if (currData.LoopId) {
            bool sameLoop = false;
            // When we are exiting a loop, we've met all the loop elements and registered their children -
            // this is by construction of the loop.
            if (state.HasIncomingDep()) {
                TNodeId loopId = prntData->LoopId;
                YDIAG(Loop) << "Exit node: LoopId = " << currData.LoopId << ", parent's = " << loopId << Endl;
                sameLoop = (loopId == currData.LoopId);
            }

            if (!sameLoop) {
                TGraphLoop& loop = Loops[currData.LoopId];
                if (!loop.DepsDone) {
                    Y_ASSERT(LoopCnt[currData.LoopId].Sign.Empty());
                    SortUnique(loop.Deps);
                    for (auto l : loop) {
                        if (l != currNode.Id()) { // TODO: THINK: just assign currentStateData.IncludedDeps to all
                            Nodes.at(l).IncludedDeps.Add(currData.IncludedDeps);
                        }
                    }

                    loop.DepsDone = true;
                }
            }
        }
    }

    // TODO: Если это раскомментировать, то ломаются тесты, которые проверяют
    // что нет разницы при запуске с кэшем или без него. Это означает, что есть
    // зависимость от нетривиального порядка вычисления: мы получаем корректные
    // окончательные значения только после того, как несколько раз выйдем из текущего узла.
    // currData.Completed = true;
}

void TJSONVisitor::Leave(TState& state) {
    if (CurEnt->EnterDepth == 1 && !CurEnt->Stored) {
        CurEnt->Stored = true;
        CurEnt->WasFresh = true;
    } else {
        CurEnt->WasFresh = false;
    }

    if (CurEnt->WasFresh && !CurEnt->Fake && CurEnt->HasBuildCmd) {
        SortedNodesForRendering.push_back(state.TopNode().Id());
        if (IsModuleType(state.TopNode()->NodeType)) {
            ++NumModuleNodesForRendering;
        }
    }
    PrepareLeaving(state);
    TBase::Leave(state);
}

void TJSONVisitor::Left(TState& state) {
    TJSONEntryStats* chldData = CurEnt;
    TBase::Left(state);
    TJSONEntryStats& currData = *CurEnt;
    TStateItem& currState = state.Top();
    bool currDone = currData.Completed;
    const auto dep = currState.CurDep();
    const auto& graph = TDepGraph::Graph(dep);
    TNodeId chldNode = dep.To().Id();

    const TStringBuf depName = graph.ToTargetStringBuf(dep.To());
    YDIAG(Dev) << "JSON: Left from " << depName << " to " << currState.Print() << Endl;
    if (!currDone && currData.WasFresh) {
        if (currData.LoopId) {
            if (currData.LoopId != chldData->LoopId && !Loops[currData.LoopId].DepsDone) {
                YDIAG(Dev) << "JSON: Leftnode was in loop = " << currData.LoopId << Endl;
                Loops[currData.LoopId].Deps.push_back(chldNode);
            }
        }
    }

    if (currData.WasFresh && currData.IsGlobalVarsCollectorStarted && IsDirectPeerdirDep(currState.CurDep())) {
        GlobalVarsCollector.Collect(currState, currState.CurDep().To());
    }
}

bool TJSONVisitor::AcceptDep(TState& state) {
    const TStateItem& currState = state.Top();
    const auto dep = state.NextDep();
    const auto currNode = state.TopNode();
    const auto& graph = TDepGraph::Graph(currNode);
    const auto currNodeType = currNode->NodeType;
    const auto chldNode = dep.To();
    TJSONEntryStats& currData = *CurEnt;
    bool currDone = currData.Completed;

    YDIAG(Dev) << "JSON: AcceptDep \"" << currState.Print() << "\" " << *dep << " \"" << graph.ToString(chldNode) << "\"" << Endl;

    if (*dep == EDT_Search2 && !IsGlobalSrcDep(dep)) {
        return false;
    }

    if (*dep == EDT_OutTogetherBack) {
        if (!currDone) {
            TStringBuf additionalOutputName = graph.ToTargetStringBuf(dep.To());
            currState.Hash->StructureMd5Update(additionalOutputName, additionalOutputName);
        }
        return false;
    }

    if (*dep == EDT_Search) {
        if (IsModuleType(currNodeType) && !currData.Fake) {
            if (!currDone && NeedAddToOuts(state, *chldNode)) {
                AddTo(chldNode.Id(), currData.ExtraOuts);
                const auto outTogetherDependency = GetOutTogetherDependency(chldNode);
                if (outTogetherDependency.IsValid()) {
                    currData.NodeDeps.Add(outTogetherDependency.Id());
                }
            }
            if (RestoreContext.Conf.DumpInputsInJSON && chldNode->NodeType == EMNT_File) {
                return true;
            }
        }
        return false;
    }

    if (*dep == EDT_Property) {
        if (IsModuleType(currNodeType) || currNodeType == EMNT_MakeFile || currNodeType == EMNT_BuildCommand || currNodeType == EMNT_BuildVariable || currNodeType == EMNT_NonParsedFile) {
            if (chldNode->NodeType == EMNT_Property) {
                TStringBuf propVal = graph.GetCmdName(chldNode).GetStr();
                TStringBuf name = GetPropertyName(propVal);
                if (IsModuleType(currNodeType) && name == "EXT_RESOURCE") {
                    TStringBuf resourceName{};
                    GetNext(propVal, ' ', resourceName);
                    const auto [it, added] = Resources.try_emplace(resourceName, propVal);
                    if (!added && it->second != propVal) {
                        YConfWarn(UserWarn) << "Bad resource " << resourceName << " " << propVal << " in module " << currState.Print()
                                        << ": was already in resources with " << it->second << Endl;
                    }
                } else if (IsModuleType(currNodeType) && name == "EXT_HOST_RESOURCE") {
                    TStringBuf mapJson = GetPropertyValue(propVal);
                    Y_ASSERT(mapJson);
                    HostResources.emplace_back(mapJson);
                } else {
                    if (!currDone && (currNodeType == EMNT_BuildCommand || currNodeType == EMNT_BuildVariable) && name == NProps::USED_RESERVED_VAR) {
                        GetOrInit(currData.UsedReservedVars).emplace(GetPropertyValue(propVal));
                    }
                }
            } else if (chldNode->NodeType == EMNT_File) {
                return state.HasIncomingDep() && IsIndirectSrcDep(state.IncomingDep());
            }
        }
        return false;
    }

    if (*dep == EDT_OutTogether) {
        currData.OutTogetherDependency = chldNode.Id();
        return true;
    }

    if (!state.HasIncomingDep() && IsDirToModuleDep(dep)) {
        if (!StartModules.contains(chldNode.Id())) {
            return false;
        }
    }

    const TModule* depModule = RestoreContext.Modules.Get(chldNode->ElemId);
    if (depModule && IsDevModuleDep(dep, *depModule)) {
        return false;
    }

    if (IsTooldirDep(currState.CurDep())) {
        return false;
    }
    const bool acc = TBase::AcceptDep(state);

    if (!acc && currNodeType == EMNT_BuildCommand && *dep == EDT_Include) {
        // Current node is a build command that depends on a macro that depends on this build command.
        TString msg;
        TStringOutput out(msg);
        out << "Macro expansion loop in " << graph.ToString(chldNode) << Endl;
        out << "Traceback:";
        for (const auto& frame : state) {
            out << Endl << "- " << frame.Print();
        }
        YErr() << msg;
        ythrow TError() << "macro expansion failed";
    }

    return acc;
}

bool TJSONVisitor::NeedAddToOuts(const TState& state, const TDepTreeNode& node) const {
    auto moduleIt = FindModule(state);
    return moduleIt != state.end() && moduleIt->Module->IsExtraOut(node.ElemId);
}
