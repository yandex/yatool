#include "json_visitor.h"

#include "action.h"
#include "json_saveload.h"
#include "module_restorer.h"
#include "prop_names.h"
#include "vars.h"

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
#include <devtools/ymake/symbols/symbols.h>

#include <library/cpp/digest/md5/md5.h>

#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/hash.h>
#include <util/generic/reserve.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/output.h>
#include <util/stream/str.h>
#include <util/string/split.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

#include <utility>

namespace {
    bool IsWrongEdge(bool mainOutputAsExtra, TDepGraph::TConstEdgeRef edgeRef) {
        if (mainOutputAsExtra) {
            return *edgeRef != EDT_Include;
        } else {
            return !IsIn({EDT_Include, EDT_OutTogether, EDT_OutTogetherBack}, *edgeRef);
        }
    }

    void DumpLoop(const TDepGraph& graph, const TGraphLoop& loop, IOutputStream& out) {
        THashSet<TNodeId> loopNodeIds{loop.begin(), loop.end()};
        for (TNodeId nodeId : loop) {
            auto nodeRef = graph[nodeId];
            out << '\t' << nodeRef->NodeType << ' ' << graph.ToString(nodeRef) << '\n';
            for (auto edgeRef : nodeRef.Edges()) {
                const char* mark = "";
                if (IsIn(loopNodeIds, edgeRef.To().Id())) {
                    mark = "* ";
                }
                out << "\t\t" << mark << *edgeRef << ' ' << graph.ToString(edgeRef.To()) << '\n';
            }
        }
    }

    bool IsCorrectLoop(const TDepGraph& graph, bool mainOutputAsExtra, const TGraphLoop& loop) {
        THashSet<TNodeId> loopNodeIds{loop.begin(), loop.end()};
        for (TNodeId nodeId : loop) {
            auto nodeRef = graph[nodeId];
            for (auto edgeRef : nodeRef.Edges()) {
                if (IsIn(loopNodeIds, edgeRef.To().Id())) {
                    if (IsWrongEdge(mainOutputAsExtra, edgeRef)) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    void CheckLoops(const TDepGraph& graph, bool mainOutputAsExtra, const TGraphLoops& loops) {
        bool first = true;
        size_t count = 0;

        for (const TGraphLoop& loop : loops) {
            if (!IsCorrectLoop(graph, mainOutputAsExtra, loop)) {
                if (first) {
                    TStringStream out;
                    DumpLoop(graph, loop, out);
                    YDebug() << "Found incorrect loop\n" << out.Str() << Endl;
                    first = false;
                }
                ++count;
            }
        }

        if (count > 0) {
            YDebug() << "Incorrect loops count: " << count << Endl;
        } else {
            YDebug() << "No incorrect loops found" << Endl;
        }
    }

    inline bool NeedToPassInputs(const TConstDepRef& dep) {
        if (dep.To()->NodeType == EMNT_Program || dep.To()->NodeType == EMNT_Library) {
            return false;
        }

        return true;
    }
}

TUidsData::TUidsData(const TRestoreContext& restoreContext, const TVector<TTarget>& startDirs)
    : TBaseVisitor{restoreContext, TDependencyFilter{TDependencyFilter::SkipRecurses}}
    , Loops(TGraphLoops::Find(restoreContext.Graph, startDirs, false))
    , LoopCnt(Loops.Ids())
{
    CacheStats.Set(NStats::EUidsCacheStats::ReallyAllNoRendered, 1); // by default all nodes really no rendered
}

void TUidsData::SaveCache(IOutputStream* output, const TDepGraph& graph) {
    TVector<ui8> rawBuffer;
    rawBuffer.reserve(64 * 1024);

    ui32 nodesCount = 0;
    for (const auto& [nodeId, nodeData] : Nodes) {
        if (nodeData.Completed) {
            ++nodesCount;
        }
    }
    output->Write(&nodesCount, sizeof(nodesCount));
    CacheStats.Set(NStats::EUidsCacheStats::SavedNodes, nodesCount);

    for (const auto& [nodeId, nodeData] : Nodes) {
        if (!nodeData.Completed) {
            continue;
        }
        TSaveBuffer buffer{&rawBuffer};
        nodeData.Save(&buffer, graph);
        buffer.SaveNodeDataToStream(output, nodeId, graph);
    }

    const TNodeId maxLoopId = Loops.MaxNodeId();
    output->Write(&maxLoopId, sizeof(maxLoopId));
    ui32 loopsCount = 0;
    for (TNodeId loopId: Loops.ValidIds()) {
        ++loopsCount;
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

void TUidsData::LoadCache(IInputStream* input, const TDepGraph& graph) {
    TVector<ui8> rawBuffer;
    rawBuffer.reserve(64 * 1024);
    ui32 nodesSkipped = 0;
    ui32 nodesDiscarded = 0;
    ui32 nodesLoaded = 0;

    ui32 nodesCount = LoadFromStream<ui32>(input);

    for (size_t i = 0; i < nodesCount; ++i) {
        TLoadBuffer buffer{&rawBuffer};
        TNodeId nodeId;
        auto loadResult = buffer.LoadUnchangedNodeDataFromStream(input, nodeId, graph, sizeof(TMd5Sig));

        if (loadResult == TLoadBuffer::NodeNotValid) {
            ++nodesSkipped;
            continue;
        }

        auto nodeRef = graph.Get(nodeId);
        Y_ASSERT(nodeRef.IsValid());

        auto [it, added] = Nodes.try_emplace(nodeRef.Id(), TJSONEntryStats::TItemDebug{graph, nodeRef.Id()});
        auto& [_, nodeData] = *it;
        nodeData.InStack = false;
        Y_ASSERT(added);

        if (loadResult == TLoadBuffer::NodeChangedAndHeaderLoaded) {
            nodeData.LoadStructureUid(&buffer, true);
            ++nodesSkipped;
            continue;
        }

        Y_ASSERT(loadResult == TLoadBuffer::NodeLoaded);

        if (!nodeData.Load(&buffer, graph)) {
            ++nodesDiscarded;
            Nodes.erase(nodeRef.Id());
            continue;
        }

        ++nodesLoaded;
    }

    CacheStats.Inc(NStats::EUidsCacheStats::SkippedNodes, nodesSkipped);
    CacheStats.Inc(NStats::EUidsCacheStats::DiscardedNodes, nodesDiscarded);
    CacheStats.Inc(NStats::EUidsCacheStats::LoadedNodes, nodesLoaded);

    const auto MaxLoopId = LoadFromStream<TNodeId>(input);
    ui32 loopsSkipped = 0;
    ui32 loopsDiscarded = 0;
    ui32 loopsLoaded = 0;

    for (TNodeId _ [[maybe_unused]]: TValidNodeIds{MaxLoopId}) {
        TLoadBuffer buffer{&rawBuffer};
        TNodeId loopNodeId;
        if (buffer.LoadUnchangedNodeDataFromStream(input, loopNodeId, graph) != TLoadBuffer::NodeLoaded) {
            ++loopsSkipped;
            continue;
        }

        if (!LoadLoop(&buffer, loopNodeId, graph)) {
            ++loopsDiscarded;
        } else {
            ++loopsLoaded;
        }
    }

    CacheStats.Inc(NStats::EUidsCacheStats::SkippedLoops, loopsSkipped);
    CacheStats.Inc(NStats::EUidsCacheStats::DiscardedLoops, loopsDiscarded);
    CacheStats.Inc(NStats::EUidsCacheStats::LoadedLoops, loopsLoaded);
}

void TUidsData::SaveLoop(TSaveBuffer* buffer, TNodeId loopId, const TDepGraph& graph) {
    const TGraphLoop& loop = Loops[loopId];

    buffer->Save(LoopCnt[loopId].SelfSign.GetRawData(), 16);
    buffer->Save(LoopCnt[loopId].Sign.GetRawData(), 16);
    buffer->Save<ui32>(loop.Deps.size());
    for (TNodeId depNode : loop.Deps) {
        buffer->SaveElemId(depNode, graph);
    }
}

bool TUidsData::LoadLoop(TLoadBuffer* buffer, TNodeId nodeFromLoop, const TDepGraph& graph) {
    const TNodeId* loopId = Loops.FindLoopForNode(nodeFromLoop);
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

TJSONVisitor::TJSONVisitor(const TRestoreContext& restoreContext, TCommands& commands, const TCmdConf& cmdConf, const TVector<TTarget>& startDirs)
    : TUidsData(restoreContext, startDirs)
    , Commands(commands)
    , CmdConf(cmdConf)
    , MainOutputAsExtra(restoreContext.Conf.MainOutputAsExtra())
    , JsonDepsFromMainOutputEnabled_(restoreContext.Conf.JsonDepsFromMainOutputEnabled())
    , GlobalVarsCollector(restoreContext)
    , Edge(restoreContext.Graph.GetInvalidEdge())
    , CurrNode(restoreContext.Graph.GetInvalidNode())
    , Graph(restoreContext.Graph)
    , ErrorShower(restoreContext.Conf.ExpressionErrorDetails.value_or(TDebugOptions::EShowExpressionErrors::None))
{
    if (JsonDepsFromMainOutputEnabled_) {
        YDebug() << "Passing JSON dependencies from main to additional outputs enabled" << Endl;
    }

    for (TTarget target : startDirs) {
        if (target.IsModuleTarget) {
            StartModules.insert(target);
        }
    }

    if (restoreContext.Conf.CheckForIncorrectLoops()) {
        CheckLoops(restoreContext.Graph, MainOutputAsExtra, Loops);
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
            bool addOutputName = true;
            if (MainOutputAsExtra) {
                if (IsMainOutput(graph, currNode.Id(), dep.To().Id())) {
                    addOutputName = false;
                }
            }

            if (addOutputName) {
                TStringBuf additionalOutputName = graph.ToTargetStringBuf(dep.To());
                currState.Hash->StructureMd5Update(additionalOutputName, additionalOutputName);
            }
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

    if (IsTooldirDep(currState.CurDep())) {
        return false;
    }
    const bool acc = TBaseVisitor::AcceptDep(state);

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

// Graph traversal rules:
// 1. Do not re-subscan Include dirs (i.e. dirs entered through non-BuildFrom edge).
// 2. Do not re-subscan built file nodes.
// 3. Always re-subscan all other nodes, unless they have no built nodes up ahead.
bool TJSONVisitor::Enter(TState& state) {
    bool fresh = TBaseVisitor::Enter(state);

    UpdateReferences(state);

    ++CurrData->EnterDepth;

    if (fresh && !CurrData->Completed) {
        Y_ASSERT(!CurrData->Finished);
        PrepareCurrent(state);
    }

    CurrData->WasVisited = true;

    if (fresh) {
        TNodeDebugOnly nodeDebug{Graph, CurrNode.Id()};

        if (CurrType == EMNT_File || CurrType == EMNT_MakeFile) {
            auto elemId = CurrNode->ElemId;
            const TFileData& fileData = Graph.Names().FileConf.GetFileDataById(elemId);
            Y_ASSERT(fileData.HashSum != TMd5Sig());
            // TODO: Inputs нужны только если включен Conf.DumpInputsMapInJSON
            // Если это редкий режим, вынести под условие.
            Inputs.push_back(std::make_pair(elemId, fileData.HashSum));
        }

        if (HasParent && *Edge == EDT_BuildFrom && !IsModuleType(CurrType)) {
            if (PrntData->Fake) {
                // Module with Fake attribute may be a program and thus not fake itself, but its own BFs are still fake
                CurrData->Fake = true;
                YDIAG(V) << "Node marked as fake: " << CurrState->Print() << " borrowed from: " << PrntState->Print() << Endl;
            }
        }

        if (const auto* loopId = Loops.FindLoopForNode(CurrNode.Id())) {
            CurrData->LoopId = *loopId;
        }

        if (IsModule(*CurrState)) {
            CurrData->IsGlobalVarsCollectorStarted = GlobalVarsCollector.Start(*CurrState);
        }
    }

    // TODO: Этот код выполняется при каждом Enter, а возможно нужно только при первом.
    if (RestoreContext.Conf.DumpInputsInJSON && CurrType == EMNT_File) {
        AddTo(CurrNode.Id(), GetNodeInputs(CurrNode.Id()));
    }

    CurrData->WasFresh = fresh;
    YDIAG(Dev) << "JSON: Enter " << CurrState->Print() << ". Freshness = " << fresh << "; LoopId = " << CurrData->LoopId << Endl;

    if (CurrType == EMNT_Directory && (!HasParent || *Edge != EDT_BuildFrom)) {
        return fresh;
    }

    if (fresh) {
        if (IsModuleType(CurrType)) {
            CurrState->Module = RestoreContext.Modules.Get(CurrNode->ElemId);
            Y_ENSURE(CurrState->Module != nullptr);
            CurrData->Fake = CurrState->Module->IsFakeModule();
        }

        if (const auto* loop = Loops.FindLoopForNode(CurrNode.Id())) {
            CurrData->LoopId = *loop;
        }
    }

    return fresh;
}

void TJSONVisitor::Leave(TState& state) {
    UpdateReferences(state);

    if (CurrData->EnterDepth == 1 && !CurrData->Stored) {
        CurrData->Stored = true;
        CurrData->WasFresh = true;
    } else {
        CurrData->WasFresh = false;
    }

    if (CurrData->WasFresh && !CurrData->Fake && CurrData->HasBuildCmd) {
        SortedNodesForRendering.push_back(state.TopNode().Id());
        TopoGenerations[CurrData->Generation].push_back(state.TopNode().Id());
        if (IsModuleType(state.TopNode()->NodeType)) {
            ++NumModuleNodesForRendering;
        }
    }

    PrepareLeaving(state);

    TNodeId chldLoop = CurrData->LoopId;
    bool inSameLoop = chldLoop != TNodeId::Invalid && PrntData && chldLoop == PrntData->LoopId;

    --CurrData->EnterDepth;
    if (CurrData->EnterDepth == 0 && !CurrData->Finished) {
        FinishCurrent(state);
        CurrData->Finished = true;
        if (chldLoop != TNodeId::Invalid && !inSameLoop) {
            ComputeLoopHash(chldLoop);
        }
    }

    if (PrntState != nullptr && !PrntData->Finished && CurrData->Finished && !inSameLoop) {
        PassToParent(state);
    }

    TBaseVisitor::Leave(state);
}

void TJSONVisitor::Left(TState& state) {
    TJSONEntryStats* chldData = CurEnt;
    TBaseVisitor::Left(state);
    TJSONEntryStats& currData = *CurEnt;
    TStateItem& currState = state.Top();
    bool currDone = currData.Completed;
    const auto dep = currState.CurDep();
    const auto& graph = TDepGraph::Graph(dep);
    TNodeId chldNode = dep.To().Id();

    const TStringBuf depName = graph.ToTargetStringBuf(dep.To());
    YDIAG(Dev) << "JSON: Left from " << depName << " to " << currState.Print() << Endl;
    if (!currDone && currData.WasFresh) {
        if (currData.LoopId != TNodeId::Invalid) {
            if (currData.LoopId != chldData->LoopId && !Loops[currData.LoopId].DepsDone) {
                YDIAG(Dev) << "JSON: Leftnode was in loop = " << currData.LoopId << Endl;
                Loops[currData.LoopId].Deps.push_back(chldNode);
            }
        }
    }

    if (currData.WasFresh && currData.IsGlobalVarsCollectorStarted && IsDirectPeerdirDep(currState.CurDep())) {
        GlobalVarsCollector.Collect(currState, currState.CurDep().To());
    }

    currData.Generation = std::max(currData.Generation, chldData->Generation + 1);
}

THashMap<TString, TMd5Sig> TJSONVisitor::GetInputs(const TDepGraph& graph) const {
    THashMap<TString, TMd5Sig> result;
    for (const auto& input : Inputs) {
        result[graph.GetFileName(input.first).GetTargetStr()] = input.second;
    }
    return result;

}

TSimpleSharedPtr<TUniqVector<TNodeId>>& TJSONVisitor::GetNodeInputs(TNodeId node) {
    if (const auto* loop = Loops.FindLoopForNode(node)) {
        return LoopsInputs[*loop];
    } else {
        return NodesInputs[node];
    }
}

const TVector<TString>& TJSONVisitor::GetHostResources() const {
    return HostResources;
}

const THashMap<TString, TString>& TJSONVisitor::GetResources() const {
    return Resources;
}

TNodeId TJSONVisitor::GetModuleByNode(TNodeId nodeId) {
    return Node2Module[nodeId];
}

void TJSONVisitor::ReportCacheStats() {
    NEvent::TNodeChanges ev;
    ev.SetHasRenderedNodeChanges(CacheStats.Get(NStats::EUidsCacheStats::ReallyAllNoRendered) ? false : true);
    FORCE_TRACE(U, ev);
    CacheStats.Report();
}

void TJSONVisitor::PrepareLeaving(TState& state) {
    bool currDone = CurrData->Completed;
    bool prntDone = false;

    if (HasParent) {
        prntDone = PrntData->Completed;
    }

    if (IsModule(*CurrState) && std::exchange(CurrData->IsGlobalVarsCollectorStarted, false)) {
        GlobalVarsCollector.Finish(*CurrState, CurrData);
    }

    // Note that the following code should be unified with Left()
    if (HasParent) {
        const auto prntNode = PrntState->Node();
        const auto incDep = PrntState->CurDep();

        const bool isParentBuildEntry = PrntData->IsFile && (*incDep == EDT_BuildFrom || *incDep == EDT_BuildCommand);
        auto& prntDestSet = isParentBuildEntry ? PrntData->NodeDeps : PrntData->IncludedDeps;
        TNodeId tool = TNodeId::Invalid;
        bool bundle = false;

        YDIAG(Dev) << "JSON: PrepareLeaving " << CurrState->Print() << "; isParentBuildEntry = " << isParentBuildEntry << Endl;
        if (CurrData->IsFile && (CurrData->HasBuildCmd || CurrData->OutTogetherDependency != TNodeId::Invalid)) {
            if (IsModuleType(CurrType) && IsDirectToolDep(incDep)) {
                auto name = PrntState->GetCmdName();
                tool = CurrNode.Id();
                bundle = Graph.Names().CommandConf.GetById(name.GetCmdId()).KeepTargetPlatform;
            } else if (!CurrData->Fake) {
                YDIAG(Dev) << "JSON: PrepareLeaving " << CurrState->Print() << "; add to " << (isParentBuildEntry ? "cmd" : "include") << " deps for " << PrntState->Print() << Endl;
                if (!prntDone)
                    prntDestSet.Add(CurrNode.Id());
                if (!currDone)
                    CurrData->IncludedDeps.Add(CurrNode.Id());
            }

            const TNodeId outTogetherDependencyId = CurrData->OutTogetherDependency;
            if (auto [node2ModuleIt, node2ModuleAdded] = Node2Module.try_emplace(CurrNode.Id(), TNodeId::Invalid); node2ModuleAdded) {
                const auto moduleState = FindModule(state); // we set it on first leaving: module should be in stack
                const bool hasModule = moduleState != state.end();
                TJSONEntryStats* moduleData = hasModule ? VisitorEntry(*moduleState) : nullptr;
                bool moduleDone = moduleData ? moduleData->Completed : false;
                if (hasModule) {
                    node2ModuleIt->second = moduleState->Node().Id();
                }

                bool addToOuts = hasModule && moduleState->Module->IsExtraOut(CurrNode->ElemId);
                if (!moduleDone && NeedAddToOuts(state, *CurrNode) && !CurrData->Fake) {
                    AddTo(CurrNode.Id(), moduleData->ExtraOuts);
                    if (outTogetherDependencyId != TNodeId::Invalid) {
                        moduleData->NodeDeps.Add(outTogetherDependencyId);
                    } else if (addToOuts) {
                        moduleData->NodeDeps.Add(CurrNode.Id());
                    }
                }
            }

            if (!prntDone && outTogetherDependencyId != TNodeId::Invalid && !CurrData->Fake) {
                YDIAG(Dev) << "JSON: PrepareLeaving " << CurrState->Print() << "; as AlsoBuilt add to " << (isParentBuildEntry ? "cmd" : "include") << " deps for " << PrntState->Print() << Endl;
                prntDestSet.Add(outTogetherDependencyId);
            }
        }

        if (tool != TNodeId::Invalid) {
            if (!prntDone) {
                if (!bundle) {
                    PrntData->NodeToolDeps.Add(tool);
                }
                prntDestSet.Add(tool);  // Note: this ensures tools in regular deps
            } else {
                if (!bundle && (!PrntData->NodeToolDeps || !PrntData->NodeToolDeps->has(tool))) {
                    YDebug() << "JSON: PrepareLeaving: cannot add tool dep " << Graph.ToTargetStringBuf(tool) << " for completed parent " << PrntState->Print() << Endl;
                }
            }
        } else {
            if (!prntDone) {
                if (!JsonDepsFromMainOutputEnabled_) {
                    if (state.Parent()->CurDep().Value() != EDT_OutTogether) {
                        prntDestSet.Add(CurrData->IncludedDeps);
                    } else {
                        prntDestSet.Add(CurrNode.Id());
                    }
                } else {
                    prntDestSet.Add(CurrData->IncludedDeps);
                }
                if (IsDirectPeerdirDep(incDep) && PrntState->Module->PassPeers()) {
                    PrntData->IncludedDeps.Add(CurrData->IncludedDeps);
                }
            }
        }

        if (IsModule(*CurrState) && CurrData->WasFresh) {
            const auto mod = CurrState->Module;
            if (mod->IsDependencyManagementApplied()) {
                TUniqVector<TNodeId>* oldDeps = CurrData->NodeDeps.Get();

                // Here we recalculate NodeDeps for nodes under dependency management.
                // First of all we should take all "managed peers"
                // calculated by dependency management, as they
                // 1) have filtered set of dependencies: only selected versions and
                // with excludes taken into the account.
                // 2) can have additional dependencies such as non-managed dependencies
                // from transitive dependencies closure.

                TUniqVector<TNodeId> deps;
                if (mod->GetPeerdirType() == EPT_BuildFrom) {
                    const auto& managedPeers = RestoreContext.Modules.GetModuleNodeLists(mod->GetId()).UniqPeers();
                    for (auto peerId : managedPeers) {
                        auto peerModule = RestoreContext.Modules.Get(Graph.Get(peerId)->ElemId);
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
                        if (IsModuleType(Graph.Get(depId)->NodeType)) {
                            continue;
                        }
                        deps.Push(depId);
                    }
                }

                // Then take NodeToolDeps as it is. No specific treatment needed.

                if (CurrData->NodeToolDeps) {
                    for (auto toolDep : *CurrData->NodeToolDeps) {
                        deps.Push(toolDep);
                    }
                }

                // Finally set new NodeDeps value, minding to leave NodeDeps holder
                // reseted when there are no values. There are supposedly some code
                // dependent on that same behaviour and I have no wish to search
                // and clean up all such places.

                if (!deps.empty()) {
                    if (!oldDeps) {
                        CurrData->NodeDeps.Reset(new TUniqVector<TNodeId>{});
                        oldDeps = CurrData->NodeDeps.Get();
                    }

                    oldDeps->swap(deps);

                } else {
                    CurrData->NodeDeps.Reset();
                }
            }
        }


        if (!prntDone && (IsInnerCommandDep(incDep) || IsBuildCommandDep(incDep))) {
            PrntData->NodeToolDeps.Add(CurrData->NodeToolDeps);
        }

        if (prntDone && (IsInnerCommandDep(incDep) || IsBuildCommandDep(incDep))) {
            if (CurrData->NodeToolDeps && CurrData->NodeToolDeps->size() > 0) {
                bool enough_deps = true;
                if (!PrntData->NodeToolDeps) {
                    enough_deps = false;
                }
                if (enough_deps) {
                    if (PrntData->NodeToolDeps->size() < CurrData->NodeToolDeps->size()) {
                        enough_deps = false;
                    }
                }
                if (enough_deps) {
                    for (auto toolDep : *CurrData->NodeToolDeps) {
                        if (!PrntData->NodeToolDeps->has(toolDep)) {
                            enough_deps = false;
                            break;
                        }
                    }
                }
                if (!enough_deps) {
                    YDebug() << "JSON: PrepareLeaving: cannot add tool deps for completed parent " << PrntState->Print() << Endl;
                }
            }
        }

        if (tool == TNodeId::Invalid && RestoreContext.Conf.DumpInputsInJSON && NeedToPassInputs(incDep)) {
            AddTo(GetNodeInputs(CurrNode.Id()), GetNodeInputs(prntNode.Id()));
        }
    }

    if (state.HasIncomingDep() && (
        IsInnerCommandDep(state.IncomingDep()) ||
        IsBuildCommandDep(state.IncomingDep()) ||
        IsLocalVariableDep(state.IncomingDep())
    )) {
        const auto usedVars = CurrData->UsedReservedVars.Get();
        if (usedVars) {
            if (!prntDone) {
                GetOrInit(PrntData->UsedReservedVars).insert(usedVars->begin(), usedVars->end());
            }
        }
    }

    if (CurrData->WasFresh) {
        CurrData->WasFresh = false;

        if (CurrData->LoopId != TNodeId::Invalid) {
            bool sameLoop = false;
            // When we are exiting a loop, we've met all the loop elements and registered their children -
            // this is by construction of the loop.
            if (state.HasIncomingDep()) {
                TNodeId loopId = PrntData->LoopId;
                YDIAG(Loop) << "Exit node: LoopId = " << CurrData->LoopId << ", parent's = " << loopId << Endl;
                sameLoop = (loopId == CurrData->LoopId);
            }

            if (!sameLoop) {
                TGraphLoop& loop = Loops[CurrData->LoopId];
                if (!loop.DepsDone) {
                    Y_ASSERT(LoopCnt[CurrData->LoopId].Sign.Empty());
                    SortUnique(loop.Deps);
                    for (auto l : loop) {
                        if (l != CurrNode.Id()) { // TODO: THINK: just assign currentStateData.IncludedDeps to all
                            Nodes.at(l).IncludedDeps.Add(CurrData->IncludedDeps);
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
    // CurrData->Completed = true;
}

void TJSONVisitor::PrepareCurrent(TState& state) {
    Y_UNUSED(state);

    TNodeDebugOnly nodeDebug{Graph, CurrNode.Id()};
    Y_ASSERT(!CurrState->Hash);
    CurrState->Hash = new TJsonMd5(nodeDebug);

    if (IsModuleType(CurrNode->NodeType) && !CurrState->Module) {
        CurrState->Module = RestoreContext.Modules.Get(CurrNode->ElemId);
        Y_ENSURE(CurrState->Module != nullptr);
    }
}

void TJSONVisitor::FinishCurrent(TState& state) {
    if (CurrNode->NodeType == EMNT_BuildCommand || CurrNode->NodeType == EMNT_BuildVariable) {
        auto name = CurrState->GetCmdName();
        auto isCmdOrCtxVar = HasParent && (
            *Edge == EDT_Include && CurrType == EMNT_BuildCommand || // TODO get rid of old-school context variables
            *Edge == EDT_BuildCommand // && (CurrType == EMNT_BuildCommand || CurrType == EMNT_BuildVariable)
        );
        if (!isCmdOrCtxVar) {
            Y_ASSERT(!name.IsNewFormat());
            auto str = name.GetStr();
            TStringBuf val = GetCmdValue(str);
            TStringBuf cmdName = GetCmdName(str);
            if (cmdName.EndsWith("__NO_UID__")) {
                YDIAG(V) << "Command not accounted for in uid: " << cmdName << ", value: " << val << Endl;
            } else {
                UpdateCurrent(state, val, "Include command value to structure uid");
            }
        } else {
            const NPolexpr::TExpression *expr = nullptr;
            if (name.IsNewFormat()) {
                // (presumably) this is a build command (expression reference, "S:123")
                expr = Commands.Get(Commands.IdByElemId(CurrState->GetCmdName().GetElemId()));
                Y_ASSERT(expr);
            } else {
                // (presumably) this is a context variable ("0:SOME_NONINLINED_VAR=S:234")
                if (CurrNode->NodeType == EMNT_BuildVariable) {
                    // a local variable
                    auto str = name.GetStr();
                    TStringBuf val = GetCmdValue(str);
                    TStringBuf cmdName = GetCmdName(str);
                    if (cmdName.EndsWith("__NO_UID__")) {
                        YDIAG(V)
                            << "Variable not accounted for in uid: " << cmdName
                            << ", value: " << Commands.PrintCmd(*Commands.Get(val, &CmdConf))
                            << Endl;
                    } else {
                        expr = Commands.Get(val, &CmdConf);
                        Y_ASSERT(expr);
                    }
                } else {
                    // a global variable: these are processed somewhere else
                    Y_ASSERT(CurrNode->NodeType == EMNT_BuildCommand && *Edge == EDT_Include);

                    if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes) {
                        auto str = name.GetStr();
                        TStringBuf val = GetCmdValue(str);
                        expr = Commands.Get(val, &CmdConf);
                        Y_ASSERT(expr);
                    }
                }
            }
            if (expr) {
                auto tagGlobal = Commands.EngineTag();
                auto tagLocal = expr->Tag();
                CurrState->Hash->StructureMd5Update({&tagGlobal, sizeof tagGlobal}, "new_cmd tag_global");
                CurrState->Hash->StructureMd5Update({&tagLocal, sizeof tagLocal}, "new_cmd tag_local");
                Commands.StreamCmdRepr(*expr, [&](auto data, auto size) {
                    CurrState->Hash->StructureMd5Update({data, size}, "new_cmd");
                });
            }
        }
        CurrState->Hash->StructureMd5Update(RestoreContext.Conf.GetUidsSalt(), "FAKEID");
    }

    // include content hash to content uid
    if (CurrNode->NodeType == EMNT_File) {
        auto elemId = CurrNode->ElemId;
        const TFileData& fileData = Graph.Names().FileConf.GetFileDataById(elemId);
        Y_ASSERT(fileData.HashSum != TMd5Sig());
        TString value = Md5SignatureAsBase64(fileData.HashSum);
        CurrState->Hash->ContentMd5Update(value, "Update content hash by current file hash sum");
        CurrState->Hash->IncludeContentMd5Update(value, "Update include content hash by current file hash sum");
    }

    if (CurrNode->NodeType == EMNT_NonParsedFile) {
        TStringBuf nodeName = Graph.ToTargetStringBuf(CurrNode);
        TMd5Value md5{TNodeDebugOnly{Graph, CurrNode.Id()}, "TJSONVisitorNew::Enter::<md5#2>"sv};
        md5.Update(nodeName, "TJSONVisitorNew::Enter::<nodeName>"sv);
        CurrState->Hash->ContentMd5Update(nodeName, "Update content hash by current file name");
        CurrState->Hash->IncludeContentMd5Update(nodeName, "Update include content hash by current file name");
    }

    // include extra output names to parent entry
    for (auto dep : CurrNode.Edges()) {
        if (*dep == EDT_OutTogetherBack) {
            bool addOutputName = true;
            if (MainOutputAsExtra) {
                if (IsMainOutput(Graph, CurrNode.Id(), dep.To().Id())) {
                    addOutputName = false;
                }
            }

            if (addOutputName)
                UpdateCurrent(state, Graph.ToTargetStringBuf(dep.To().Id()), "Include extra output name to structure uid");
        }
    }

    // include owners and module tag
    if (IsModuleType(CurrNode->NodeType)) {
        auto tag = CurrState->Module->GetTag();
        if (tag) {
            UpdateCurrent(state, tag, "Include module tag to structure uid");
            if (CurrState->Module->IsFromMultimodule()) {
                // In fact tag is only emitted into node for multimodules, so make distinction
                UpdateCurrent(state, "mm", "Include multimodule tag to structure uid");
            }
        }

        // IncludeStructure hash should have an additional mark for fake modules.
        // There can be no other differences in hashes for a case when an ordinary module
        // becomes fake. E.g. it could happen when there was GLOBAL and non-GLOBAL SRCS first,
        // and then all sources became GLOBAL. When this happens the module would disappear
        // from $PEERS and corresponding command line arguments where PEERS closure happens.
        // But without this "fake" mark we have no way to know that the command structure changed
        // as the graph structure remains mostly the same (the difference is there are
        // no GROUP_NAME=ModuleInputs entries in module and that's almost all).
        if (CurrState->Module->IsFakeModule()) {
            CurrState->Hash->IncludeStructureMd5Update("FakeModuleTag"_sb, "Add fake module tag"sv);
        }

        // include managed peers closure to structure hash
        if (CurrState->Module->IsDependencyManagementApplied() && CurrData->NodeDeps) {
            for (TNodeId nodeId : *CurrData->NodeDeps) {
                const TNodeData* chldState = Nodes.FindPtr(nodeId);
                Y_ASSERT(chldState);

                if (chldState) {
                    const auto depNode = RestoreContext.Graph.Get(nodeId);

                    if (IsMainOutput(Graph, depNode.Id(), chldState->OutTogetherDependency)) {
                        continue;
                    }

                    const auto name = RestoreContext.Graph.ToTargetStringBuf(depNode);
                    UpdateCurrent(state, name, "Include managed peer name to structure hash");

                    CurrState->Hash->IncludeStructureMd5Update(
                        chldState->GetIncludeStructureUid(),
                        "Pass IncludeStructure for managed peer"
                    );
                }
            }
        }
    }

    if (CurrData->OutTogetherDependency != TNodeId::Invalid) {
        // When a command works with a generated file that is an additional output,
        // the command should depend on the corresponding main output node, because
        // it is associated to it's build command and the JSON node, which in turn
        // will be in the command's JSON node dependencies.
        // As for now we store all such dependencies in NodeDeps, and we store all nodes
        // of input files there too. Thus we can not distinguish such main output node
        // dependency from a real input file, and the main output file name will be
        // listed in the "inputs" section of JSON node, even though this file is not
        // present in the command arguments and is not really used.
        // So we should include main output name in the IncludeStructure hash
        // because when the name changes, all JSON nodes whose commands include this file
        // will change too, specifically theirs "inputs" sections.
        // TODO: This is a workaround, and should be removed when "inputs" sections
        // no more contain spurious dependency main outputs.
        bool addOutputName = true;
        if (MainOutputAsExtra) {
            if (IsMainOutput(Graph, CurrNode.Id(), CurrData->OutTogetherDependency)) {
                addOutputName = false;
            }
        }

        if (addOutputName) {
            TStringBuf mainOutputName = Graph.ToTargetStringBuf(Graph.Get(CurrData->OutTogetherDependency));
            CurrState->Hash->IncludeStructureMd5Update(mainOutputName, "Include main output name"sv);
        }
    }

    // Node name will be in $AUTO_INPUT for the consumer of this node.
    if (IsFileType(CurrNode->NodeType)) {
        CurrState->Hash->IncludeStructureMd5Update(Graph.ToTargetStringBuf(CurrNode), "Node name for $AUTO_INPUT");
    }

    // There is no JSON node for current DepGraph node
    if (CurrData->HasBuildCmd) {
        AddAddincls(state);

        AddGlobalVars(state);

        AddGlobalSrcs();

        // include main output name to cur entry
        if (IsFileType(CurrNode->NodeType)) {
            TStringBuf nodeName = Graph.ToTargetStringBuf(CurrNode);
            UpdateCurrent(state, nodeName, "Include node name to current structure hash");

            // include module_tag to non-module nodes and if module if from multimodule
            if (!IsModuleType(CurrNode->NodeType)) {
                const auto moduleIt = FindModule(state);
                if (moduleIt != state.end() && moduleIt->Module->IsFromMultimodule()) {
                    UpdateCurrent(state, moduleIt->Module->GetTag(), "Include module tag to current structure hash");
                }
            }
        }
    }

    if (UseFileId(CurrNode->NodeType) || CurrNode->NodeType == EMNT_BuildCommand) {
        CurrData->SetContentUid(CurrState->Hash->GetContentMd5());
        CurrData->SetIncludeContentUid(CurrState->Hash->GetIncludeContentMd5());
    }

    CurrData->SetStructureUid(CurrState->Hash->GetStructureMd5());
    CurrData->SetIncludeStructureUid(CurrState->Hash->GetIncludeStructureMd5());
    CurrData->Finished = true;
    CurrData->Completed = true;
    if (CurrData->LoopId == TNodeId::Invalid) {
        CheckStructureUidChanged(*CurrData);
    }
}

void TJSONVisitor::PassToParent(TState& state) {
    // include inputs name
    bool isBuildFromDep = *Edge == EDT_BuildFrom && IsFileType(CurrNode->NodeType);
    if (isBuildFromDep && CurrNode->NodeType != EMNT_File) {
        TStringBuf nodeName = Graph.ToTargetStringBuf(CurrNode);
        UpdateParent(state, nodeName, "Include input name to parent structure hash");
    }

    // include build command
    if (IsBuildCommandDep(Edge) || IsInnerCommandDep(Edge)) {
        UpdateParent(state, CurrData->GetStructureUid(), "Include build cmd name to parent structure hash");
    }

    // tool dep
    if (CurrNode->NodeType == EMNT_Program && IsDirectToolDep(Edge)) {
        TStringBuf nodeName = Graph.ToTargetStringBuf(CurrNode);
        UpdateParent(state, nodeName, "Include tool node name to parent structure hash");
    }

    // local variables
    if (*Edge == EDT_BuildCommand && CurrNode->NodeType == EMNT_BuildVariable) {
        UpdateParent(state, CurrData->GetStructureUid(), "Include local variables");
    }

    // global variables in non-modules
    if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes) {
        if (Edge.From()->NodeType == EMNT_NonParsedFile && *Edge == EDT_Include && CurrNode->NodeType == EMNT_BuildCommand) {
            UpdateParent(state, CurrData->GetStructureUid(), "Include global variables (non-module version)");
        }
    }

    // late globs
    if (*Edge == EDT_BuildFrom && CurrNode->NodeType == EMNT_BuildCommand) {
        UpdateParent(state, CurrData->GetStructureUid(), "Include late glob hash to parent structure hash");
        PrntState->Hash->ContentMd5Update(CurrData->GetIncludeContentUid(), "Add late glob include content hash"_sb);
    }

    if (*Edge == EDT_Property && IsFileType(CurrNode->NodeType)) {
        TStringBuf nodeName = Graph.ToTargetStringBuf(CurrNode);
        UpdateParent(state, nodeName, "Include file names to late glob structure hash");
        PrntState->Hash->IncludeContentMd5Update(CurrData->GetContentUid(), "Add file content hash to late glob content include hash"_sb);
    }

    if (IsFileType(CurrNode->NodeType) && IsFileType(PrntState->Node()->NodeType)) {
        bool isModuleDep = IsDirectPeerdirDep(Edge) && PrntState->Module->PassPeers();
        if (*Edge == EDT_Include || isModuleDep) {
            bool isDMModules = false;
            if (isModuleDep) {
                if (!CurrState->Module) {
                    CurrState->Module = RestoreContext.Modules.Get(CurrNode->ElemId);
                    Y_ENSURE(CurrState->Module != nullptr);
                }
                isDMModules = CurrState->Module->GetAttrs().RequireDepManagement && PrntState->Module->GetAttrs().RequireDepManagement;
            }
            if (!isDMModules) {
                PrntState->Hash->IncludeStructureMd5Update(CurrData->GetIncludeStructureUid(), "Pass IncludeStructure by EDT_Include");
            }
        }

        if (*Edge == EDT_BuildFrom) {
            PrntState->Hash->StructureMd5Update(CurrData->GetIncludeStructureUid(), "Pass IncludeStructre by EDT_BuildFrom");
        }
    }

    if (IsGlobalSrcDep(Edge)) {
        TStringBuf globalSrcName = Graph.ToTargetStringBuf(CurrNode);
        PrntState->Hash->IncludeStructureMd5Update(globalSrcName, "Add GlobalSrc name"sv);

        PrntState->Hash->IncludeStructureMd5Update(CurrData->GetIncludeStructureUid(), "Add IncludeStructure from GlobalSrc"sv);
    }

    if (IsIncludeFileDep(Edge)) {
        PrntState->Hash->IncludeContentMd5Update(CurrData->GetIncludeContentUid(), "Pass IncludeContent by EDT_Include"sv);
    }

    if (*Edge == EDT_BuildFrom && IsSrcFileType(CurrNode->NodeType)) {
        if (CurrNode->NodeType != EMNT_NonParsedFile) {
            PrntState->Hash->ContentMd5Update(CurrData->GetContentUid(), "Update parent content UID with content"sv);
        }
        PrntState->Hash->ContentMd5Update(CurrData->GetIncludeContentUid(), "Update parent content UID with include content"sv);
    }
}

bool TJSONVisitor::NeedAddToOuts(const TState& state, const TDepTreeNode& node) const {
    auto moduleIt = FindModule(state);
    return moduleIt != state.end() && moduleIt->Module->IsExtraOut(node.ElemId);
}

void TJSONVisitor::UpdateParent(TState& state, TStringBuf value, TStringBuf description) {
    const auto& parentState = *state.Parent();
    const auto& graph = TDepGraph::Graph(state.TopNode());

    YDIAG(Dev) << description << ": " << value << " " << graph.ToString(parentState.Node()) << Endl;
    parentState.Hash->StructureMd5Update(value, value);
}

void TJSONVisitor::UpdateParent(TState& state, const TMd5SigValue& value, TStringBuf description) {
    const auto& parentState = *state.Parent();
    const auto& graph = TDepGraph::Graph(state.TopNode());

    TStringBuf nodeName = graph.ToTargetStringBuf(state.TopNode());

    YDIAG(Dev) << description << ": " << nodeName << " " << graph.ToString(parentState.Node()) << Endl;
    parentState.Hash->StructureMd5Update(value, nodeName);
}

void TJSONVisitor::UpdateCurrent(TState& state, TStringBuf value, TStringBuf description) {
    const auto& graph = TDepGraph::Graph(state.TopNode());

    YDIAG(Dev) << description << ": " << value << " " << graph.ToString(state.TopNode()) << Endl;
    state.Top().Hash->StructureMd5Update(value, value);
}

void TJSONVisitor::AddAddincls(TState& state) {
    const auto moduleIt = FindModule(state);
    bool hasModule = moduleIt != state.end();
    if (!hasModule || !moduleIt->Module || !CurrData->UsedReservedVars) {
        return;
    }

    const auto& includesMap = moduleIt->Module->IncDirs.GetAll();
    const auto& usedVars = *CurrData->UsedReservedVars;
    for (size_t lang = 0; lang < NLanguages::LanguagesCount(); lang++) {
        auto&& includeVarName = TModuleIncDirs::GetIncludeVarName(static_cast<TLangId>(lang));
        if (!usedVars.contains(includeVarName)) {
            continue;
        }
        moduleIt->Module->IncDirs.MarkLanguageAsUsed(static_cast<TLangId>(lang));
        const auto includesIt = includesMap.find(static_cast<TLangId>(lang));
        if (includesIt == includesMap.end()) {
            continue;
        }
        for (const auto& dir : includesIt->second.Get()) {
            TStringBuf dirStr = dir.GetTargetStr();
            UpdateCurrent(state, dirStr, "Include addincl dir to current structure hash");
        }
    }
}

void TJSONVisitor::AddGlobalVars(TState& state) {
    const auto moduleIt = FindModule(state);
    bool hasModule = moduleIt != state.end();
    if (!hasModule || !moduleIt->Module || !CurrData->UsedReservedVars) {
        return;
    }

    TAutoPtr<THashSet<TString>> seenVars;

    auto addVarsFromModule = [&](ui32 moduleElemId) {
        for (const auto& [varName, varValue] : RestoreContext.Modules.GetGlobalVars(moduleElemId).GetVars()) {
            if (CurrData->UsedReservedVars->contains(varName)) {
                if (seenVars) {
                    auto [_, added] = seenVars->insert(varName);
                    if (!added)
                        continue;
                }

                for (const auto& varItem : varValue) {
                    if (varItem.StructCmdForVars) {
                        Y_DEBUG_ABORT_UNLESS(!varItem.HasPrefix);
                        auto expr = Commands.Get(varItem.Name, &CmdConf);
                        Y_ASSERT(expr);
                        UpdateCurrent(state, varName, "new_cmd_name");
                        auto tagGlobal = Commands.EngineTag();
                        auto tagLocal = expr->Tag();
                        CurrState->Hash->StructureMd5Update({&tagGlobal, sizeof tagGlobal}, "new_cmd tag_global");
                        CurrState->Hash->StructureMd5Update({&tagLocal, sizeof tagLocal}, "new_cmd tag_local");
                        Commands.StreamCmdRepr(*expr, [&](auto data, auto size) {
                            CurrState->Hash->StructureMd5Update({data, size}, "new_cmd");
                        });
                    } else {
                        TString value = FormatProperty(varName, varItem.Name);
                        UpdateCurrent(state, value, "Include global var to current structure hash");
                    }
                }
            }
        }
    };

    ui32 moduleElemId = moduleIt->Module->GetId();
    addVarsFromModule(moduleElemId);

    // Here we also look at GlobalVars from peers under dependency management.
    // When collecting GlobalVars in TGlobalVarsCollector we skip propagation for such peers,
    // as GlobalVars set can not be derived from GlobalVars in direct peers.

    // E.g. module B can depend on module C and has it's global variables,
    // but a module A depending on module B can however exclude module C and
    // will not has it's global variables despite module B has them and is a direct peer of A.

    // So we can't fill GlobalVars while doing traverse by TGlobalVarsCollector. And we can't fill
    // them later by a separate traverse or other post-processing, as we already need them now in
    // JSON visitor and TGlobalVarsCollector traverse is embedded in it.

    // We could have two sets of "GlobalVars": the usual one and the one that arise
    // from dependency managament, but not participate in transitive propagation. But anyway
    // we will have to use them both here (and maybe at other places). So it is almost the same.

    // There is somewhat equivalent code in TModuleRestorer::UpdateGlobalVarsFromModule,
    // which collects dependency management global variables values in the same fashion
    // when a JSON rendering takes place. But it does a little more work than needed here
    // and is being called only when we have missed JSON cache.
    // So I think no such code reuse is suitable.

    TModule* module = RestoreContext.Modules.Get(moduleElemId);
    if (module->GetAttrs().RequireDepManagement) {
        seenVars.Reset(new THashSet<TString>{});
        for (const auto& [varName, _] : RestoreContext.Modules.GetGlobalVars(moduleElemId).GetVars()) {
            seenVars->insert(varName);
        }

        const auto& managedPeersListId = RestoreContext.Modules.GetModuleNodeLists(moduleElemId).UniqPeers();
        for (TNodeId peerNodeId : managedPeersListId) {
            ui32 peerModuleElemId = RestoreContext.Graph[peerNodeId]->ElemId;
            addVarsFromModule(peerModuleElemId);
        }
    }
}

void TJSONVisitor::AddGlobalSrcs() {
    if(!IsModuleType(CurrType) || !CurrData->UsedReservedVars) {
        return;
    }

    if (CurrData->UsedReservedVars->contains("SRCS_GLOBAL")) {
        TModuleRestorer restorer(RestoreContext, CurrNode);
        restorer.RestoreModule();
        for (TNodeId globalSrcNodeId : restorer.GetGlobalSrcsIds()) {
            TNodeData* data = Nodes.FindPtr(globalSrcNodeId);
            Y_ASSERT(data);
            if (!data)
                continue;

            // All generated GLOBAL_SRCS nodes are added to NodeDeps and their changes
            // are accounted for in Full UID as dependencies
            if (data->HasBuildCmd)
                continue;

            CurrState->Hash->ContentMd5Update(data->GetContentUid(), "GlobalSrc content"_sb);
            CurrState->Hash->ContentMd5Update(data->GetIncludeContentUid(), "GlobalSrc include content"_sb);

            if (RestoreContext.Conf.DumpInputsInJSON) {
                TConstDepNodeRef srcNode = Graph[globalSrcNodeId];
                Y_ASSERT(srcNode->NodeType == EMNT_File);
                if (srcNode->NodeType == EMNT_File) {
                    AddTo(globalSrcNodeId, GetNodeInputs(CurrNode.Id()));
                }
            }
        }
    }
}

void TJSONVisitor::ComputeLoopHash(TNodeId loopId) {
    const TGraphLoop& loop = Loops[loopId];

    TJsonMultiMd5 structureLoopHash(loopId, Graph.Names(), loop.size());
    TJsonMultiMd5 includeStructureLoopHash(loopId, Graph.Names(), loop.size());
    TJsonMultiMd5 contentLoopHash(loopId, Graph.Names(), loop.size());
    TJsonMultiMd5 includeContentLoopHash(loopId, Graph.Names(), loop.size());

    for (const auto& nodeId : loop) {
        const auto nodeDataIt = Nodes.find(nodeId);
        if (nodeDataIt == Nodes.end()) {
            // Loops now contains directories for peerdirs while this visitor
            // skips such directories by direct module-to-module edges
            continue;
        }

        const TJSONEntryStats& nodeData = nodeDataIt->second;
        TStringBuf nodeName = Graph.ToTargetStringBuf(Graph.Get(nodeId));
        structureLoopHash.AddSign(nodeData.GetStructureUid(), nodeName, true);
        includeStructureLoopHash.AddSign(nodeData.GetIncludeStructureUid(), nodeName, true);
        contentLoopHash.AddSign(nodeData.GetContentUid(), nodeName, true);
        includeContentLoopHash.AddSign(nodeData.GetIncludeContentUid(), nodeName, true);
    }

    TMd5SigValue structureLoopUid{"TJSONVisitorNew::StructureLoopUID"sv};
    TMd5SigValue includeStructureLoopUid{"TJSONVisitorNew::IncludeStructureLoopUID"sv};
    TMd5SigValue contentLoopUid{"TJSONVisitorNew::ContentLoopUID"sv};
    TMd5SigValue includeContentLoopUid{"TJSONVisitorNew::IncludeContentLoopUID"sv};
    structureLoopHash.CalcFinalSign(structureLoopUid);
    includeStructureLoopHash.CalcFinalSign(includeStructureLoopUid);
    contentLoopHash.CalcFinalSign(contentLoopUid);
    includeContentLoopHash.CalcFinalSign(includeContentLoopUid);

    for (const auto& node : loop) {
        const auto nodeDataIt = Nodes.find(node);
        if (nodeDataIt == Nodes.end()) {
            continue;
        }

        TJSONEntryStats& nodeData = nodeDataIt->second;
        nodeData.SetIncludeStructureUid(includeStructureLoopUid);
        nodeData.SetContentUid(contentLoopUid);
        nodeData.SetIncludeContentUid(includeContentLoopUid);

        if (!nodeData.HasBuildCmd) {
            nodeData.SetStructureUid(structureLoopUid);

        } else {
            TMd5Value structureHash{"Loop structure hash with name"sv};
            structureHash.Update(structureLoopUid, "Common loop structure hash"sv);
            TStringBuf mainOutputName = Graph.ToTargetStringBuf(Graph.Get(node));
            structureHash.Update(mainOutputName, "Main output name"sv);

            TMd5SigValue structureUid{"Loop structure uid with name"sv};
            structureUid.MoveFrom(std::move(structureHash));

            nodeData.SetStructureUid(structureUid);
        }
        nodeData.Finished = true;
        nodeData.Completed = true;
        CheckStructureUidChanged(nodeData);
    }
}

void TJSONVisitor::UpdateReferences(TState& state) {
    CurrState = &state.Top();
    CurrData = CurEnt;

    HasParent = state.HasIncomingDep();

    if (HasParent) {
        PrntState = &*state.Parent();
        PrntData = reinterpret_cast<TNodeData*>(PrntState->Cookie);
    } else {
        PrntState = nullptr;
        PrntData = nullptr;
    }

    CurrNode.~TNodeRefBase();
    new (&CurrNode) TDepGraph::TConstNodeRef(state.TopNode());

    Edge.~TEdgeRefBase();
    new (&Edge) TDepGraph::TConstEdgeRef(state.IncomingDep());

    CurrType = CurrNode->NodeType;
}

void TJSONVisitor::CheckStructureUidChanged(const TJSONEntryStats& data) {
    if (data.GetStructureUid().GetRawSig() != data.GetPreStructureUid().GetRawSig()) {
        CacheStats.Set(NStats::EUidsCacheStats::ReallyAllNoRendered, 0); // at least one node rendered
    } else {
        // TODO Research and try load it from cache
    }
}
