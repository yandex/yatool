#include "json_visitor_new.h"

#include "action.h"
#include "module_restorer.h"
#include "parser_manager.h"

TJSONVisitorNew::TJSONVisitorNew(const TRestoreContext& restoreContext, TCommands& commands, const TCmdConf &cmdConf, const TVector<TTarget>& startDirs)
    : TBase{restoreContext, TDependencyFilter{TDependencyFilter::SkipRecurses}}
    , Commands(commands)
    , CmdConf(cmdConf)
    , MainOutputAsExtra(restoreContext.Conf.MainOutputAsExtra())
    , Edge(restoreContext.Graph.GetInvalidEdge())
    , CurrNode(restoreContext.Graph.GetInvalidNode())
{
    CacheStats.Set(NStats::EUidsCacheStats::ReallyAllNoRendered, 1); // by default all nodes really no rendered
    Loops.FindLoops(RestoreContext.Graph, startDirs, false);
}


bool TJSONVisitorNew::AcceptDep(TState& state) {
    return TBase::AcceptDep(state);
}

bool TJSONVisitorNew::Enter(TState& state) {
    bool fresh = TBase::Enter(state);

    UpdateReferences(state);

    ++CurrData->EnterDepth;

    if (fresh && !CurrData->Completed) {
        Y_ASSERT(!CurrData->Finished);
        PrepareCurrent(state);
    }

    return fresh;
}

void TJSONVisitorNew::Leave(TState& state) {
    UpdateReferences(state);

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

    TBase::Leave(state);
}

void TJSONVisitorNew::Left(TState& state) {
    TBase::Left(state);
}

void TJSONVisitorNew::PrepareCurrent(TState& state) {
    Y_UNUSED(state);

    TNodeDebugOnly nodeDebug{*Graph, CurrNode.Id()};
    Y_ASSERT(!CurrState->Hash);
    CurrState->Hash = new TJsonMd5(nodeDebug);

    if (IsModuleType(CurrNode->NodeType) && !CurrState->Module) {
        CurrState->Module = RestoreContext.Modules.Get(CurrNode->ElemId);
        Y_ENSURE(CurrState->Module != nullptr);
    }
}

void TJSONVisitorNew::FinishCurrent(TState& state) {
    if (CurrNode->NodeType == EMNT_BuildCommand || CurrNode->NodeType == EMNT_BuildVariable) {
        auto name = CurrState->GetCmdName();
        if (!PrntData->StructCmdDetected) {
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
            if (expr)
                Commands.StreamCmdRepr(*expr, [&](auto data, auto size) {
                    CurrState->Hash->StructureMd5Update({data, size}, "new_cmd");
                });
        }
        CurrState->Hash->StructureMd5Update(RestoreContext.Conf.GetUidsSalt(), "FAKEID");
    }

    // include content hash to content uid
    if (CurrNode->NodeType == EMNT_File) {
        auto elemId = CurrNode->ElemId;
        const TFileData& fileData = Graph->Names().FileConf.GetFileDataById(elemId);
        Y_ASSERT(fileData.HashSum != TMd5Sig());
        TString value = Md5SignatureAsBase64(fileData.HashSum);
        CurrState->Hash->ContentMd5Update(value, "Update content hash by current file hash sum");
        CurrState->Hash->IncludeContentMd5Update(value, "Update include content hash by current file hash sum");
    }

    if (CurrNode->NodeType == EMNT_NonParsedFile) {
        TStringBuf nodeName = Graph->ToTargetStringBuf(CurrNode);
        TMd5Value md5{TNodeDebugOnly{*Graph, CurrNode.Id()}, "TJSONVisitorNew::Enter::<md5#2>"sv};
        md5.Update(nodeName, "TJSONVisitorNew::Enter::<nodeName>"sv);
        CurrState->Hash->ContentMd5Update(nodeName, "Update content hash by current file name");
        CurrState->Hash->IncludeContentMd5Update(nodeName, "Update include content hash by current file name");
    }

    // include extra output names to parent entry
    for (auto dep : CurrNode.Edges()) {
        if (*dep == EDT_OutTogetherBack) {
            bool addOutputName = true;
            if (MainOutputAsExtra) {
                if (IsMainOutput(*Graph, CurrNode.Id(), dep.To().Id())) {
                    addOutputName = false;
                }
            }

            if (addOutputName)
                UpdateCurrent(state, Graph->ToTargetStringBuf(dep.To().Id()), "Include extra output name to structure uid");
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

                    if (IsMainOutput(*Graph, depNode.Id(), chldState->OutTogetherDependency)) {
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
            if (IsMainOutput(*Graph, CurrNode.Id(), CurrData->OutTogetherDependency)) {
                addOutputName = false;
            }
        }

        if (addOutputName) {
            TStringBuf mainOutputName = Graph->ToTargetStringBuf(Graph->Get(CurrData->OutTogetherDependency));
            CurrState->Hash->IncludeStructureMd5Update(mainOutputName, "Include main output name"sv);
        }
    }

    // Node name will be in $AUTO_INPUT for the consumer of this node.
    if (IsFileType(CurrNode->NodeType)) {
        CurrState->Hash->IncludeStructureMd5Update(Graph->ToTargetStringBuf(CurrNode), "Node name for $AUTO_INPUT");
    }

    // There is no JSON node for current DepGraph node
    if (CurrData->HasBuildCmd) {
        AddAddincls(state);

        AddGlobalVars(state);

        // include main output name to cur entry
        if (IsFileType(CurrNode->NodeType)) {
            TStringBuf nodeName = Graph->ToTargetStringBuf(CurrNode);
            UpdateCurrent(state, nodeName, "Include node name to current structure hash");
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
    CheckStructureUidChanged();
}

void TJSONVisitorNew::PassToParent(TState& state) {
    // include inputs name
    bool isBuildFromDep = *Edge == EDT_BuildFrom && IsFileType(CurrNode->NodeType);
    if (isBuildFromDep && CurrNode->NodeType != EMNT_File) {
        TStringBuf nodeName = Graph->ToTargetStringBuf(CurrNode);
        UpdateParent(state, nodeName, "Include input name to parent structure hash");
    }

    // include build command
    if (IsBuildCommandDep(Edge) || IsInnerCommandDep(Edge)) {
        UpdateParent(state, CurrData->GetStructureUid(), "Include build cmd name to parent structure hash");
    }

    // tool dep
    if (CurrNode->NodeType == EMNT_Program && IsDirectToolDep(Edge)) {
        TStringBuf nodeName = Graph->ToTargetStringBuf(CurrNode);
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
        TStringBuf nodeName = Graph->ToTargetStringBuf(CurrNode);
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
        TStringBuf globalSrcName = Graph->ToTargetStringBuf(CurrNode);
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

void TJSONVisitorNew::UpdateParent(TState& state, TStringBuf value, TStringBuf description) {
    const auto& parentState = *state.Parent();
    const auto& graph = TDepGraph::Graph(state.TopNode());

    YDIAG(Dev) << description << ": " << value << " " << graph.ToString(parentState.Node()) << Endl;
    parentState.Hash->StructureMd5Update(value, value);
}

void TJSONVisitorNew::UpdateParent(TState& state, const TMd5SigValue& value, TStringBuf description) {
    const auto& parentState = *state.Parent();
    const auto& graph = TDepGraph::Graph(state.TopNode());

    TStringBuf nodeName = graph.ToTargetStringBuf(state.TopNode());

    YDIAG(Dev) << description << ": " << nodeName << " " << graph.ToString(parentState.Node()) << Endl;
    parentState.Hash->StructureMd5Update(value, nodeName);
}

void TJSONVisitorNew::UpdateCurrent(TState& state, TStringBuf value, TStringBuf description) {
    const auto& graph = TDepGraph::Graph(state.TopNode());

    YDIAG(Dev) << description << ": " << value << " " << graph.ToString(state.TopNode()) << Endl;
    state.Top().Hash->StructureMd5Update(value, value);
}

void TJSONVisitorNew::AddAddincls(TState& state) {
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

void TJSONVisitorNew::AddGlobalVars(TState& state) {
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
                    if (varItem.StructCmd) {
                        Y_DEBUG_ABORT_UNLESS(!varItem.HasPrefix);
                        auto expr = Commands.Get(varItem.Name, &CmdConf);
                        Y_ASSERT(expr);
                        UpdateCurrent(state, varName, "new_cmd_name");
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

        const auto managedPeersListId = RestoreContext.Modules.GetModuleNodeIds(moduleElemId).UniqPeers;
        for (TNodeId peerNodeId : RestoreContext.Modules.GetNodeListStore().GetList(managedPeersListId)) {
            ui32 peerModuleElemId = RestoreContext.Graph[peerNodeId]->ElemId;
            addVarsFromModule(peerModuleElemId);
        }
    }
}

void TJSONVisitorNew::ComputeLoopHash(TNodeId loopId) {
    const TGraphLoop& loop = Loops[AsIdx(loopId)];

    TJsonMultiMd5 structureLoopHash(loopId, Graph->Names(), loop.size());
    TJsonMultiMd5 includeStructureLoopHash(loopId, Graph->Names(), loop.size());
    TJsonMultiMd5 contentLoopHash(loopId, Graph->Names(), loop.size());
    TJsonMultiMd5 includeContentLoopHash(loopId, Graph->Names(), loop.size());

    for (const auto& nodeId : loop) {
        const auto nodeDataIt = Nodes.find(nodeId);
        if (nodeDataIt == Nodes.end()) {
            // Loops now contains directories for peerdirs while this visitor
            // skips such directories by direct module-to-module edges
            continue;
        }

        const TJSONEntryStats& nodeData = nodeDataIt->second;
        TStringBuf nodeName = Graph->ToTargetStringBuf(Graph->Get(nodeId));
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
            TStringBuf mainOutputName = Graph->ToTargetStringBuf(Graph->Get(node));
            structureHash.Update(mainOutputName, "Main output name"sv);

            TMd5SigValue structureUid{"Loop structure uid with name"sv};
            structureUid.MoveFrom(std::move(structureHash));

            nodeData.SetStructureUid(structureUid);
        }
        nodeData.Finished = true;
        nodeData.Completed = true;
        CheckStructureUidChanged();
    }
}

void TJSONVisitorNew::UpdateReferences(TState& state) {
    CurrState = &state.Top();
    CurrData = CurEnt;

    if (state.HasIncomingDep()) {
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

    const auto node = CurrState->Node();
    Graph = &TDepGraph::Graph(node);
}

void TJSONVisitorNew::CheckStructureUidChanged() {
    if (CurrData->GetStructureUid().GetRawSig() != CurrData->GetPreStructureUid().GetRawSig()) {
        CacheStats.Set(NStats::EUidsCacheStats::ReallyAllNoRendered, 0); // at least one node rendered
    } else {
        // TODO Research and try load it from cache
    }
}

void TJSONVisitorNew::ReportCacheStats() {
    NEvent::TNodeChanges ev;
    ev.SetHasRenderedNodeChanges(CacheStats.Get(NStats::EUidsCacheStats::ReallyAllNoRendered) ? false : true);
    FORCE_TRACE(U, ev);
    CacheStats.Report();
}
