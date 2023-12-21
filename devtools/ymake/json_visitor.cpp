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
    TString SerializeTargetProperties(const TModule* mod, ERenderModuleType renderModuleType, bool isGlobalNode) {
        TString props(Reserve(32));
        TStringOutput out(props);
        out << '|' << ToString(renderModuleType) << '|';
        if (mod->IsFromMultimodule()) {
            out << mod->GetTag() << '|';
        }
        if (isGlobalNode) {
            out << "global"sv << '|';
        }
        return props;
    }
}

inline bool NeedToPassInputs(const TConstDepRef& dep) {
    if (dep.To()->NodeType == EMNT_Program || dep.To()->NodeType == EMNT_Library) {
        return false;
    }

    return true;
}

TJSONVisitor::TJSONVisitor(const TRestoreContext& restoreContext, TCommands& commands, const TVector<TTarget>& startDirs, bool newUids)
    : TBase{restoreContext, commands, startDirs, newUids}
    , Commands{commands}
    , GlobalVarsCollector(restoreContext)
{
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

    ui32 nodesCount = Nodes.size();
    output->Write(&nodesCount, sizeof(nodesCount));

    for (const auto& [nodeId, nodeData] : Nodes) {
        TSaveBuffer buffer{&rawBuffer};
        nodeData.Save(&buffer, graph, NewUids);
        buffer.SaveNodeDataToStream(output, nodeId, graph);
    }

    CacheStats.Set(NStats::EUidsCacheStats::SavedNodes, Nodes.size());

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
        if (!buffer.LoadUnchangedNodeDataFromStream(input, &nodeId, graph)) {
            CacheStats.Inc(NStats::EUidsCacheStats::SkippedNodes);
            continue;
        }

        auto nodeRef = graph.Get(nodeId);

        auto [it, added] = Nodes.try_emplace(nodeRef.Id(), TJSONEntryStats::TItemDebug{graph, nodeRef.Id()});
        auto& [_, nodeData] = *it;
        nodeData.InStack = false;
        nodeData.InitUids(NewUids);
        Y_ASSERT(added);

        if (!nodeData.Load(&buffer, graph, NewUids)) {
            CacheStats.Inc(NStats::EUidsCacheStats::DiscardedNodes);
            Nodes.erase(nodeRef.Id());
        }

        CacheStats.Inc(NStats::EUidsCacheStats::LoadedNodes);
    }

    ui32 loopsCount = LoadFromStream<ui32>(input);

    for (size_t i = 0; i < loopsCount; ++i) {
        TLoadBuffer buffer{&rawBuffer};
        TNodeId loopNodeId;
        if (!buffer.LoadUnchangedNodeDataFromStream(input, &loopNodeId, graph)) {
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
    bool currDone = currData.Completed;

    currData.WasVisited = true;

    TStateItem* prntState = nullptr;
    TJSONEntryStats* prntData = nullptr;
    if (state.HasIncomingDep()) {
        prntState = &*state.Parent();
        prntData = VisitorEntry(*prntState);
    }

    if (fresh) {
        TNodeDebugOnly nodeDebug{graph, node.Id()};
        if (!currState.Hash) {
            currState.Hash = new TJsonMd5Old(nodeDebug, graph.ToString(node), graph.Names());
        }

        if (nodeType == EMNT_File || nodeType == EMNT_MakeFile) {
            auto elemId = currState.Node()->ElemId;
            const TFileData& fileData = graph.Names().FileConf.GetFileDataById(elemId);
            Y_ASSERT(fileData.HashSum != TMd5Sig());
            // TODO: Inputs нужны только если включен Conf.DumpInputsMapInJSON
            // Если это редкий режим, вынести под условие.
            Inputs.push_back(std::make_pair(elemId, fileData.HashSum));

            if (!currDone && !NewUids) {
                TMd5Value md5{nodeDebug, "TJSONVisitor::Enter::<md5#1>"sv};
                md5.Update(fileData.HashSum.RawData, sizeof(fileData.HashSum.RawData), "TJSONVisitor::Enter::<fileData.HashSum>"sv);
                md5.Update(currState.Hash->Old()->GetName(), "TJSONVisitor::Enter::<currentState.Hash->GetName()>"sv);
                currData.OldUids()->SetContextSign(md5, currState.Hash->Old()->GetId(), TStringBuf(""));
                currData.OldUids()->SetSelfContextSign(md5, currState.Hash->Old()->GetId(), TStringBuf(""));
            }
        }

        if (!RestoreContext.Conf.BlackList.Empty() && IsFileType(nodeType) && !IsOutputType(nodeType)) {
            auto& fileConf = RestoreContext.Graph.Names().FileConf;
            TFileView fileView = graph.GetFileName(currState.Node());
            const auto path = fileConf.ResolveLink(fileView);
            if (path.GetType() != NPath::Source) {
                // Do nothing
            } else if (const auto ptr = RestoreContext.Conf.BlackList.IsValidPath(path.GetTargetStr())) {
                const auto moduleIt = FindModule(state);
                THolder<TScopedContext> pcontext{nullptr};
                if (moduleIt != state.end()) {
                    pcontext.Reset(new TScopedContext(graph.GetFileName(moduleIt->Node())));
                }
                YConfErr(BlckLst) << "Trying to use [[imp]]" << path
                    << "[[rst]] from the prohibited directory [[alt1]]"
                    << *ptr << "[[rst]]" << Endl;
            }
        }

        if (!currDone && !NewUids && state.HasIncomingDep() && IsGlobalSrcDep(state.IncomingDep())) {
            const auto* mod = prntState->Module;
            Y_ENSURE(mod != nullptr);
            if (mod->GetGlobalLibId() == node->ElemId) {
                TString props = SerializeTargetProperties(mod, ERenderModuleType::Library, /* isGlobalNode */ true);
                currState.Hash->Old()->ContextMd5Update(props.data(), props.size());
                currState.Hash->Old()->RenderMd5Update(props.data(), props.size());
            }
        }

        if (state.HasIncomingDep() && *state.IncomingDep() == EDT_BuildFrom && !IsModuleType(nodeType)) {
            if (prntData->Fake) {
                // Module with Fake attribute may be a program and thus not fake itself, but its own BFs are still fake
                currData.Fake = true;
                YDIAG(V) << "Node marked as fake: " << currState.Print() << " borrowed from: " << prntState->Print() << Endl;
            }
        }

        if (!currDone && !NewUids && nodeType == EMNT_BuildCommand && (!state.HasIncomingDep() || !IsIndirectSrcDep(state.IncomingDep()))) {
            TMd5Value md5{nodeDebug, "TJSONVisitor::Enter::<md5#2>"sv};
            auto name = currState.GetCmdName();

            if (!name.IsNewFormat()) {
                auto str = name.GetStr();
                TStringBuf val = GetCmdValue(str);
                TStringBuf cmdName = GetCmdName(str);
                if (cmdName.EndsWith("__NO_UID__")) {
                    YDIAG(V) << "Command not accounted for in uid: " << cmdName << ", value: " << val << Endl;
                } else {
                    md5.Update(val.data(), val.size(), "TJSONVisitor::Enter::<cmd value>"sv);
                }
                currState.Hash->Old()->RenderMd5Update(val.data(), val.size());
            } else {
                auto expr = Commands.Get(Commands.IdByElemId(currState.GetCmdName().GetElemId()));
                Y_ASSERT(expr);
                Commands.StreamCmdRepr(*expr, [&](auto data, auto size) {
                    md5.Update(data, size, "TJSONVisitor::Enter::<StreamCmdRepr>"sv);
                    currState.Hash->Old()->RenderMd5Update(data, size);
                });
            }
            currData.OldUids()->SetContextSign(md5, currState.Hash->Old()->GetId(), RestoreContext.Conf.GetUidsSalt());
            currData.OldUids()->SetSelfContextSign(md5, currState.Hash->Old()->GetId(), RestoreContext.Conf.GetUidsSalt());
            currData.OldUids()->SetRenderId(currState.Hash->Old()->GetRenderMd5(), currState.Hash->Old()->GetId());
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

    if (!NewUids) {
        fresh |= !currData.HasUsualEntry;
        if (!state.HasIncomingDep() || *state.IncomingDep() != EDT_OutTogether) {
            currData.HasUsualEntry = true;
        }
    }

    if (fresh) {
        if (!currState.Hash) {
            currState.Hash = new TJsonMd5Old(TNodeDebugOnly{graph, node.Id()}, graph.ToString(node), graph.Names());
        }

        if (IsModuleType(nodeType)) {
            currState.Module = RestoreContext.Modules.Get(node->ElemId);
            Y_ENSURE(currState.Module != nullptr);
            currData.Fake = currState.Module->IsFakeModule();
        }

        if (!currDone && !NewUids && IsModuleType(nodeType)) {
            if (currData.Fake) {
                YDIAG(V) << "Module node marked as fake: " << currState.Module->GetName() << Endl;
            } else {
                const auto* mod = currState.Module;
                const auto renderModuleType = static_cast<ERenderModuleType>(mod->GetAttrs().RenderModuleType);
                TString props = SerializeTargetProperties(mod, renderModuleType, /* isGlobalNode */ false);
                currState.Hash->Old()->ContextMd5Update(props.data(), props.size());
                currState.Hash->Old()->RenderMd5Update(props.data(), props.size());
            }
        }

        if (const auto it = Loops.Node2Loop.find(node.Id())) {
            currData.LoopId = it->second;
        }
    }

    if (nodeType == EMNT_BuildCommand && state.HasIncomingDep() && *state.IncomingDep() == EDT_BuildCommand) {
        if (currState.GetCmdName().IsNewFormat()) {
            prntData->StructCmdDetected = true;
        }
    }

    return fresh;
}

void TJSONVisitor::PrepareLeaving(TState& state) {
    const TStateItem& currState = state.Top();
    const auto currNode = currState.Node();
    const auto& graph = TDepGraph::Graph(currNode);
    TJSONEntryStats& currData = *CurEnt;
    Name = currState.Print();
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

        YDIAG(Dev) << "JSON: PrepareLeaving " << Name << "; isParentBuildEntry = " << isParentBuildEntry << Endl;
        if (currData.IsFile && (currData.HasBuildCmd || currData.OutTogetherDependency != 0)) {
            if (IsModuleType(currNode->NodeType) && IsDirectToolDep(incDep)) {
                auto name = prntState->GetCmdName();
                tool = currNode.Id();
                bundle = graph.Names().CommandConf.GetById(name.GetCmdId()).KeepTargetPlatform;
            } else if (!currData.Fake) {
                YDIAG(Dev) << "JSON: PrepareLeaving " << Name << "; add to " << (isParentBuildEntry ? "cmd" : "include") << " deps for " << prntState->Print() << Endl;
                if (!prntDone)
                    AddTo(currNode.Id(), prntDestSet);
                if (!currDone)
                    AddTo(currNode.Id(), currData.IncludedDeps);
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
                    if (!NewUids) {
                        moduleState->Hash->Old()->ContextMd5Update(Name.data(), Name.size()); // only once, because we set Node2Module once.
                        moduleState->Hash->Old()->RenderMd5Update(Name.data(), Name.size());
                        moduleState->Hash->Old()->SelfContextMd5Update(Name.data(), Name.size());
                    }
                    if (outTogetherDependencyId) {
                        AddTo(outTogetherDependencyId, moduleData->NodeDeps);
                    } else if (addToOuts) {
                        AddTo(currNode.Id(), moduleData->NodeDeps);
                    }
                }

                // Update context md5 for files here if module provides special info for global vars
                if (hasModule && currData.UsedReservedVars && !NewUids) {
                    if (!currDone) {
                        for (const auto& varStr : RestoreContext.Modules.GetGlobalVars(moduleState->Module->GetId()).GetVars()) {
                            if (currData.UsedReservedVars->contains(varStr.first)) {
                                for (const auto& varItem : varStr.second) {
                                    TString value = FormatProperty(varStr.first, varItem.Name);
                                    currState.Hash->Old()->ContextMd5Update(value.data(), value.size());
                                    currState.Hash->Old()->RenderMd5Update(value.data(), value.size());
                                    currState.Hash->Old()->SelfContextMd5Update(value.data(), value.size());
                                }
                            }
                        }
                    }

                    if (moduleState->Module) {
                        const auto& includesMap = moduleState->Module->IncDirs.GetAll();
                        const auto& usedVars = *currData.UsedReservedVars;
                        for (size_t lang = 0; lang < NLanguages::LanguagesCount(); lang++) {
                            auto&& includeVarName = TModuleIncDirs::GetIncludeVarName(static_cast<TLangId>(lang));
                            if (usedVars.contains(includeVarName)) {
                                moduleState->Module->IncDirs.MarkLanguageAsUsed(static_cast<TLangId>(lang));
                                if (!currDone) {
                                    const auto includesIt = includesMap.find(static_cast<TLangId>(lang));
                                    if (includesIt != includesMap.end()) {
                                        for (const auto& dir : includesIt->second.Get()) {
                                            TStringBuf dirStr = dir.GetTargetStr();
                                            currState.Hash->Old()->ContextMd5Update(dirStr.data(), dirStr.size());
                                            currState.Hash->Old()->RenderMd5Update(dirStr.data(), dirStr.size());
                                            currState.Hash->Old()->SelfContextMd5Update(dirStr.data(), dirStr.size());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!prntDone && outTogetherDependencyId && !currData.Fake) {
                YDIAG(Dev) << "JSON: PrepareLeaving " << Name << "; as AlsoBuilt add to " << (isParentBuildEntry ? "cmd" : "include") << " deps for " << prntState->Print() << Endl;
                AddTo(outTogetherDependencyId, prntDestSet);
            }
        }

        if (tool) {
            if (!prntDone) {
                if (!bundle) {
                    AddTo(tool, prntData->NodeToolDeps);
                }
                AddTo(tool, prntDestSet);  // Note: this ensures tools in regular deps
            }
        } else {
            if (!prntDone) {
                if (NewUids) {
                    if (state.Parent()->CurDep().Value() != EDT_OutTogether) {
                        AddTo(currData.IncludedDeps, prntDestSet);
                    } else {
                        AddTo(currNode.Id(), prntDestSet);
                    }
                } else {
                    AddTo(currData.IncludedDeps, prntDestSet);
                }
                if (IsDirectPeerdirDep(incDep) && prntState->Module->PassPeers()) {
                    AddTo(currData.IncludedDeps, prntData->IncludedDeps);
                }
            }
        }

        if (IsModule(currState) && currData.WasFresh) {
            const auto mod = currState.Module;
            if (mod->IsDependencyManagementApplied() && currData.NodeDeps) {
                TUniqVector<TNodeId> deps;
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

                for (auto depId : *currData.NodeDeps) {
                    auto depNode = graph.Get(depId);
                    if (IsModuleType(depNode->NodeType)) {
                        continue;
                    }
                    deps.Push(depId);
                }

                if (currData.NodeToolDeps) {
                    for (auto toolDep : *currData.NodeToolDeps) {
                        deps.Push(toolDep);
                    }
                }

                if (currData.NodeDeps->size() != deps.size()) {
                    deps.swap(*currData.NodeDeps);
                }
            }
        }


        if (!prntDone && IsInnerCommandDep(incDep) || IsBuildCommandDep(incDep)) {
            AddTo(currData.NodeToolDeps, prntData->NodeToolDeps);
        }

        if (!tool && RestoreContext.Conf.DumpInputsInJSON && NeedToPassInputs(incDep)) {
            AddTo(GetNodeInputs(currNode.Id()), GetNodeInputs(prntNode.Id()));
        }
    }

    if (state.HasIncomingDep() && (IsInnerCommandDep(state.IncomingDep()) || IsBuildCommandDep(state.IncomingDep()))) {
        const auto usedVars = currData.UsedReservedVars.Get();
        if (usedVars) {
            if (!prntDone) {
                GetOrInit(prntData->UsedReservedVars).insert(usedVars->begin(), usedVars->end());
            }

            if (!currDone && !NewUids && currData.WasFresh && IsBuildCommandDep(state.IncomingDep()) && IsModule(*prntState)) {
                const auto mod = prntState->Module;
                if (mod->IsDependencyManagementApplied()) {
                    const auto& lists = RestoreContext.Modules.GetNodeListStore();
                    const auto& ids = RestoreContext.Modules.GetModuleNodeIds(mod->GetId());
                    for (const auto varName : {"MANAGED_PEERS_CLOSURE"sv, "MANAGED_PEERS"sv}) {
                        if (!usedVars->contains(varName)) {
                            continue;
                        }
                        const auto& managedPeers = (varName == "MANAGED_PEERS"sv) ? ids.ManagedDirectPeers : ids.UniqPeers;
                        for (TNodeId peer : lists.GetList(managedPeers)) {
                            const auto peerNode = RestoreContext.Graph.Get(peer);
                            const auto name = RestoreContext.Graph.GetNameFast(peerNode);
                            currState.Hash->Old()->IncludesMd5Update(name.data(), name.size());
                            currData.IncludesMd5Started = true;
                            currState.Hash->Old()->ContextMd5Update(name.data(), name.size());
                            currState.Hash->Old()->SelfContextMd5Update(name.data(), name.size());
                            currState.Hash->Old()->RenderMd5Update(name.data(), name.size());
                        }
                    }
                }
            }
        }
    }

    if (currData.WasFresh) {
        currData.WasFresh = false;
        if (!currDone && !NewUids && currData.HasBuildCmd) {
            currState.Hash->Old()->ContextMd5Update(Name.data(), Name.size());
            currData.OldUids()->SetContextSign(currState.Hash->Old()->GetContextMd5(), currState.Hash->Old()->GetId(), RestoreContext.Conf.GetUidsSalt());
            currState.Hash->Old()->RenderMd5Update(Name.data(), Name.size());
            currState.Hash->Old()->SelfContextMd5Update(Name.data(), Name.size());
            currData.OldUids()->SetSelfContextSign(currState.Hash->Old()->GetSelfContextMd5(), currState.Hash->Old()->GetId(), RestoreContext.Conf.GetUidsSalt());
        }

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
                    CalcLoopSig(currData.LoopId, LoopCnt[currData.LoopId], loop, graph);
                    for (auto l : loop) {
                        if (l != currNode.Id()) { // TODO: THINK: just assign currentStateData.IncludedDeps to all
                            AddTo(currData.IncludedDeps, Nodes.at(l).IncludedDeps);
                        }
                    }

                    loop.DepsDone = true;
                }
            }
        } else {
            if (!currDone && !NewUids) {
                if (currData.IncludesMd5Started) {
                    currData.OldUids()->SetIncludedContextSign(currState.Hash->Old()->GetIncludesMd5());
                    currData.OldUids()->SetIncludedSelfContextSign(currState.Hash->Old()->GetIncludesSelfContextMd5());
                } else {
                    currData.OldUids()->SetIncludedContextSign(currData.OldUids()->GetContextSign());
                    if (!IsOutputType(currNode->NodeType)) {
                        currData.OldUids()->SetIncludedSelfContextSign(currData.OldUids()->GetSelfContextSign());
                    } else {
                        currState.Hash->Old()->SelfContextMd5Update(Name.data(), Name.size());
                        currData.OldUids()->SetIncludedSelfContextSign(currState.Hash->Old()->GetIncludesSelfContextMd5());
                    }
                }
            }
        }

        if (!currDone && !NewUids && IsModuleType(currNode->NodeType)) {
            auto tag = currState.Module->GetTag();
            if (tag) {
                currState.Hash->Old()->RenderMd5Update(tag.data(), tag.size());
                if (currState.Module->IsFromMultimodule()) {
                    // In fact tag is only emitted into node for multimodules, so make distinction
                    currState.Hash->Old()->RenderMd5Update("mm", 2);
                }
            }
        }

        if (!currDone && !NewUids) {
            currData.OldUids()->SetRenderId(currState.Hash->Old()->GetRenderMd5(), currState.Hash->Old()->GetId());
        }
    }

    if (state.HasIncomingDep() && !NewUids) {
        const auto incDep = prntState->CurDep();

        if (!prntDone && IsModule(*prntState) && IsGlobalSrcDep(incDep)) {
            prntState->Hash->Old()->IncludesMd5Update(currData.OldUids()->GetContextSign(), Name);
            prntState->Hash->Old()->ContextMd5Update(currData.OldUids()->GetContextSign(), Name);
            prntState->Hash->Old()->RenderMd5Update(Name.data(), Name.size());
            if (!IsOutputType(currNode->NodeType)) {
                prntState->Hash->Old()->SelfContextMd5Update(currData.OldUids()->GetSelfContextSign(), Name);
                prntState->Hash->Old()->IncludesSelfContextMd5Update(currData.OldUids()->GetSelfContextSign(), Name);
            } else {
                prntState->Hash->Old()->SelfContextMd5Update(Name.data(), Name.size());
                prntState->Hash->Old()->IncludesSelfContextMd5Update(Name.data(), Name.size());
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
    if (NewUids) {
        if (CurEnt->NewUids()->EnterDepth == 1 && !CurEnt->NewUids()->Stored) {
            CurEnt->NewUids()->Stored = true;
            CurEnt->WasFresh = true;
        } else {
            CurEnt->WasFresh = false;
        }
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

    const TString depName = graph.ToString(dep.To());
    YDIAG(Dev) << "JSON: Left from " << depName << " to " << currState.Print() << Endl;
    if (!currDone && currData.WasFresh) {
        const TMd5SigValue& dependencySign = chldData->LoopId ? LoopCnt[chldData->LoopId].Sign : *dep == EDT_OutTogether ? chldData->OldUids()->GetContextSign() : chldData->OldUids()->GetIncludedContextSign();
        const TMd5SigValue& selfDependencySign = chldData->LoopId ? LoopCnt[chldData->LoopId].SelfSign : *dep == EDT_OutTogether ? chldData->OldUids()->GetSelfContextSign() : chldData->OldUids()->GetIncludedSelfContextSign();
        bool isALiftedVariable = currData.StructCmdDetected && *dep == EDT_Include && dep.To()->NodeType == EMNT_BuildCommand;
        if (!NewUids && currData.IsFile && (*dep == EDT_BuildFrom || *dep == EDT_BuildCommand || isALiftedVariable)) {
            if (!chldData->LoopId && *dep == EDT_BuildFrom) {
                currState.Hash->Old()->ContextMd5Update(chldData->OldUids()->GetContextSign(), depName);
                currState.Hash->Old()->ContextMd5Update(chldData->OldUids()->GetIncludedContextSign(), depName);
                if (!IsOutputType(dep.To()->NodeType)) {
                    currState.Hash->Old()->SelfContextMd5Update(chldData->OldUids()->GetSelfContextSign(), depName);
                    currState.Hash->Old()->SelfContextMd5Update(chldData->OldUids()->GetIncludedSelfContextSign(), depName);
                } else {
                    currState.Hash->Old()->SelfContextMd5Update(depName.data(), depName.size());
                    currState.Hash->Old()->SelfContextMd5Update(chldData->OldUids()->GetIncludedSelfContextSign(), depName);
                }
            } else {
                if (chldData->LoopId) {
                    currState.Hash->Old()->ContextMd5Update(dependencySign,
                                                        NUidDebug::LoopNodeName(chldData->LoopId));
                    currState.Hash->Old()->SelfContextMd5Update(selfDependencySign,
                                                            NUidDebug::LoopNodeName(chldData->LoopId));
                } else {
                    currState.Hash->Old()->ContextMd5Update(dependencySign, depName);
                    if (!IsOutputType(dep.To()->NodeType)) {
                        currState.Hash->Old()->SelfContextMd5Update(selfDependencySign, depName);
                    } else {
                        currState.Hash->Old()->SelfContextMd5Update(depName.data(), depName.size());
                    }
                }
            }

            currState.Hash->Old()->RenderMd5Update(chldData->OldUids()->GetRenderId(), depName);
        }

        if (!NewUids && IsInnerCommandDep(dep)) {
            currState.Hash->Old()->RenderMd5Update(chldData->OldUids()->GetRenderId(), depName);
        }

        if (currData.LoopId) {
            if (currData.LoopId != chldData->LoopId && !Loops[currData.LoopId].DepsDone) {
                YDIAG(Dev) << "JSON: Leftnode was in loop = " << currData.LoopId << Endl;
                Loops[currData.LoopId].Deps.push_back(chldNode);
            }
        } else {
            if (!NewUids) {
                // When we have no children (IncludesMd5Started) we'll just use original file's md5 value
                if (!currData.IncludesMd5Started) {
                    currState.Hash->Old()->IncludesMd5Update(currData.OldUids()->GetContextSign(), currState.Hash->Old()->GetName());
                    if (!IsOutputType(currState.Node()->NodeType)) {
                        currState.Hash->Old()->IncludesSelfContextMd5Update(currData.OldUids()->GetSelfContextSign(), currState.Hash->Old()->GetName());
                    } else {
                        currState.Hash->Old()->IncludesSelfContextMd5Update(currState.Hash->Old()->GetName().data(), currState.Hash->Old()->GetName().size());
                    }
                    currData.IncludesMd5Started = true;
                }
                // Unlike loops case, we don't need to bother about order and uniq'ness of children,
                // because when set of children changes, even their order, our own md5 changes, too.
                if (chldData->LoopId) {
                    currState.Hash->Old()->IncludesMd5Update(dependencySign,
                                                        NUidDebug::LoopNodeName(chldData->LoopId));
                } else {
                    currState.Hash->Old()->IncludesMd5Update(dependencySign, depName);
                }
                if (!IsOutputType(currState.Node()->NodeType) || (*dep != EDT_BuildFrom && *dep != EDT_BuildCommand && *dep != EDT_OutTogether)) {
                    currState.Hash->Old()->IncludesSelfContextMd5Update(selfDependencySign, depName);
                } else if (*dep == EDT_OutTogether) {
                    currState.Hash->Old()->IncludesSelfContextMd5Update(depName.data(), depName.size());
                }
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
        if (NewUids) {
            if (!currDone) {
                TString additionalOutputName = graph.ToString(dep.To());
                currState.Hash->New()->StructureMd5Update(additionalOutputName, additionalOutputName);
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
                    AddTo(outTogetherDependency.Id(), currData.NodeDeps);
                }
            }
            if (RestoreContext.Conf.DumpInputsInJSON && chldNode->NodeType == EMNT_File) {
                return true;
            }
        }
        return false;
    }

    if (*dep == EDT_Property) {
        if (IsModuleType(currNodeType) || currNodeType == EMNT_MakeFile || currNodeType == EMNT_BuildCommand || currNodeType == EMNT_NonParsedFile) {
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
                    bool nodeHasLiftedVariables = currData.StructCmdDetected && (currNodeType == EMNT_NonParsedFile || IsModuleType(currNodeType));
                    if (!currDone && (currNodeType == EMNT_BuildCommand || nodeHasLiftedVariables) && name == NProps::USED_RESERVED_VAR) {
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

        if (NewUids) {
            return true;
        }

        if (!currDone) {
            const auto dependencyDataIt = Nodes.find(chldNode.Id());
            if (dependencyDataIt != Nodes.end()) {
                if (dependencyDataIt->second.InStack) {
                    TNodeId chldNodeId = chldNode.Id();
                    auto resIt = state.FindRecent(state.begin() + 1, [chldNodeId](const TStateItem& what) { return what.Node().Id() == chldNodeId; });
                    AssertEx(resIt != state.end(), "InStack was true but not found in stack! " << graph.GetFileName(chldNode));

                    TMd5SigValue mainDepSig{TNodeDebugOnly{resIt->Node()}, "TJSONVisitor::AcceptDep::<mainDepSig>"sv};
                    TMd5Value mainDepCopy = resIt->Hash->Old()->GetContextMd5(); //BuildFrom and BuildCommand for main dep were already processed by design
                    // Btw let's check it:
                    Y_ASSERT(dependencyDataIt->second.HasBuildCmd && dependencyDataIt->second.HasBuildFrom);
                    mainDepSig.MoveFrom(std::move(mainDepCopy));
                    currState.Hash->Old()->IncludesMd5Update(mainDepSig, resIt->Hash->Old()->GetName());
                    if (!CurEnt->IncludesMd5Started) {
                        CurEnt->IncludesMd5Started = true; // Don't miss Includes hash
                    }
                    YDIAG(Dev) << graph.GetFileName(chldNode) << ": update IncludesMd5 with copy of ContextMd5 of main dep in stack" << mainDepSig.ToBase64() << Endl;
                }
            }
        }
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

void TJSONVisitor::CalcLoopSig(TNodeId loopId, TLoopCnt& loopHash, TGraphLoop& loop, const TDepGraph& graph) {
    SortUnique(loop.Deps);
    YDIAG(Loop) << "Loop " << loopId << Endl;
    TJsonMultiMd5 loopMd5(loopId, graph.Names(), loop.Deps.size() + loop.size());
    TJsonMultiMd5 selfLoopMd5(loopId, graph.Names(), loop.Deps.size() + loop.size());

    for (const auto& dependency : loop.Deps) {
        const auto dependencyDataIt = Nodes.find(dependency);
        Y_ASSERT(dependencyDataIt != Nodes.end());

        const TJSONEntryStats& dependencyData = dependencyDataIt->second;
        Y_ASSERT(dependencyData.LoopId != loopId);

        if (dependencyData.LoopId) {
            loopMd5.AddSign(LoopCnt[dependencyData.LoopId].Sign, NUidDebug::LoopNodeName(dependencyData.LoopId), false);
            selfLoopMd5.AddSign(LoopCnt[dependencyData.LoopId].SelfSign, NUidDebug::LoopNodeName(dependencyData.LoopId), false);
        } else {
            TString nodeName = graph.ToString(graph.Get(dependency));
            loopMd5.AddSign(dependencyData.OldUids()->GetIncludedContextSign(), nodeName, false);
            if (!IsOutputType(graph.Get(dependency)->NodeType)) {
                selfLoopMd5.AddSign(dependencyData.OldUids()->GetIncludedSelfContextSign(), nodeName, false);
            } else {
                TMd5SigValue mainDepSig{"TJSONVisitor::CalcLoopSig::<mainDepSig>"sv};
                TMd5Value mainDepCopy{"TJSONVisitor::CalcLoopSig::<mainDepCopy>"sv};
                mainDepCopy.Update(nodeName, "TJSONVisitor::CalcLoopSig::<nodeName>"sv);
                mainDepSig.MoveFrom(std::move(mainDepCopy));
                selfLoopMd5.AddSign(mainDepSig, nodeName, false);
            }
        }
    }

    for (const auto& nodeId : loop) {
        const auto nodeDataIt = Nodes.find(nodeId);
        if (nodeDataIt == Nodes.end()) {
            // Loops now contains directories for peerdirs while this visitor
            // skips such directories by direct module-to-module edges
            continue;
        }

        const TJSONEntryStats& nodeData = nodeDataIt->second;
        TString nodeName = graph.ToString(graph.Get(nodeId));
        loopMd5.AddSign(nodeData.OldUids()->GetContextSign(), nodeName, true);
        selfLoopMd5.AddSign(nodeData.OldUids()->GetSelfContextSign(), nodeName, true);
    }

    loopMd5.CalcFinalSign(loopHash.Sign);
    selfLoopMd5.CalcFinalSign(loopHash.SelfSign);
    YDIAG(Loop) << "Loop " << loopId << ": " << loopMd5.Size() << " signs, res = " << loopHash.Sign.ToBase64() << Endl;

    for (const auto& node : loop) {
        const auto nodeDataIt = Nodes.find(node);
        if (nodeDataIt == Nodes.end()) {
            continue;
        }
        TJSONEntryStats& nodeData = nodeDataIt->second;
        TNodeDebugOnly nodeDebug{graph, node};
        const TMd5SigValue nodeOldMd5Sig = nodeData.OldUids()->GetContextSign();
        TMd5Value nodeNewMd5{nodeDebug, "TJSONVisitor::CalcLoopSig::<newNodeMd5>"sv};
        nodeNewMd5.Update(nodeOldMd5Sig, "TJSONVisitor::CalcLoopSig::<nodeOldMd5Sig>"sv);
        nodeNewMd5.Update(loopHash.Sign, "TJSONVisitor::CalcLoopSig::<loopHash.Sign>"sv);
        const TMd5SigValue nodeOldSelfMd5Sig = nodeData.OldUids()->GetSelfContextSign();
        TMd5Value nodeNewSelfMd5{nodeDebug, "TJSONVisitor::CalcLoopSig::<newNodeSelfMd5>"sv};
        nodeNewSelfMd5.Update(nodeOldSelfMd5Sig, "TJSONVisitor::CalcLoopSig::<nodeOldSelfMd5Sig>"sv);
        nodeNewSelfMd5.Update(loopHash.SelfSign, "TJSONVisitor::CalcLoopSig::<loopHash.SelfSign>"sv);
        TString nodeName;
        graph.GetFileName(graph.Get(node)).GetStr(nodeName);
        const auto nodeDebugId = NUidDebug::GetNodeId(nodeName, graph.Names());
        NUidDebug::LogDependency(nodeDebugId, loopMd5.GetLoopNodeId());
        nodeData.OldUids()->SetContextSign(nodeNewMd5, nodeDebugId, RestoreContext.Conf.GetUidsSalt());
        nodeData.OldUids()->SetSelfContextSign(nodeNewSelfMd5, nodeDebugId, RestoreContext.Conf.GetUidsSalt());
        YDIAG(Dev)
            << "Update ContextMd5, " << nodeName << " += " << loopHash.Sign.ToBase64()
            << " (was " << nodeOldMd5Sig.ToBase64()
            << ", become " << nodeData.OldUids()->GetContextSign().ToBase64() << ")" << Endl;
    }
}

bool TJSONVisitor::NeedAddToOuts(const TState& state, const TDepTreeNode& node) const {
    auto moduleIt = FindModule(state);
    return moduleIt != state.end() && moduleIt->Module->IsExtraOut(node.ElemId);
}
