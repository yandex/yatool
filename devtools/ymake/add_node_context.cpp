#include "add_node_context.h"

#include "module_loader.h"
#include "module_builder.h"
#include "add_dep_adaptor_inline.h"

#include <util/generic/algorithm.h>

namespace {
    TVector<ui32> PropsListToDirsIds(const TPropsNodeList& props) {
        TVector<ui32> dirIds;
        for (const auto& cacheId : props) {
            Y_ASSERT(IsFile(cacheId));
            dirIds.push_back(ElemId(cacheId));
        }
        return dirIds;
    }
}

template <class V>
size_t FindInStack(V& stack, EMakeNodeType type, ui64 elemId) {
    bool fType = UseFileId(type);
    for (size_t n = 1; n < stack.size() - 1;  ++n)
        if (UseFileId(stack[n].Node.NodeType) == fType && stack[n].Node.ElemId == elemId)
            return n;
    return 0;
}

TNodeAddCtx::TNodeAddCtx(TModule* module, TYMake& yMake, TUpdEntryStats& entry, bool isMod, TAddIterStack* stack, TFileHolder& fileContent, EReadFileContentMethod readMethod)
        : Module(module)
        , YMake(yMake)
        , Graph(YMake.Graph)
        , UpdIter(*YMake.UpdIter)
        , Entry(entry)
        , IsModule(isMod)
{
    NeedInit2 = !stack;
    if (stack) {
        Init2(*stack, fileContent, module, readMethod);
    }
}

TModAddData& TNodeAddCtx::GetModuleData() {
    const auto cacheId = MakeDepsCacheId(NodeType, ElemId);
    TModAddData* modData = UpdIter.GetAddedModuleInfo(cacheId);
    Y_ASSERT(modData != nullptr);
    return *modData;
}

void TNodeAddCtx::GetOldDeps(TDeps& deps, size_t numFirst, bool allOutputs) {
    Y_ASSERT(UpdNode != TNodeId::Invalid);

    const TDepGraph::TConstNodeRef node = Graph.Get(UpdNode);
    size_t numDeps = node.Edges().Total();
    numDeps = numFirst && !allOutputs ? Min<size_t>(numFirst, numDeps) : numDeps;

    deps.Clear(); //??
    deps.Reserve(numDeps);
    for (const auto& edge : node.Edges()) {
        if (deps.Size() == numDeps) {
            break;
        }

        deps.Add(edge.Value(), edge.To()->NodeType, edge.To()->ElemId);
    }
}

void TNodeAddCtx::UseOldDeps() {
    TDeps oldDeps;
    GetOldDeps(oldDeps, 0, true);

    auto& delayedDeps = UpdIter.DelayedSearchDirDeps.GetNodeDepsByType({NodeType, static_cast<ui32>(ElemId)}, EDT_Search);
    Y_ASSERT(Deps.Empty());
    Y_ASSERT(delayedDeps.empty());

    for (const auto& dep : oldDeps) {
        if (IsSearchDirDep(NodeType, dep.DepType, dep.NodeType)) {
            YDIAG(GUpd) << "Delaying old dep " << NodeType << " " << Graph.ToString(Graph.GetNodeById(NodeType, ElemId))
                        << " -" << dep.DepType << "> "
                        << dep.NodeType << " " << " " << dep.ElemId << " "
                        << Graph.GetFileName(dep.NodeType, dep.ElemId) << Endl;
            delayedDeps.Push(dep.ElemId);
        } else {
            Deps.Add(dep.DepType, dep.NodeType, dep.ElemId);
        }
    }
}

void TNodeAddCtx::AddInputs() {
    if (!Module) {
        return;
    }
    if (IsModule) {
        //YDIAG(Dev) << "IsModule: " << ElemId << Endl;
        if (ModuleDef == nullptr || &ModuleDef->GetModule() != Module) {
            // Retriable with -xx
            ythrow TNotImplemented() << "Module was not loaded properly to Node: " <<  Graph.GetFileName(ElemId);
        }
        AssertEx(ModuleBldr == nullptr && !Module->IsInputsComplete(), "Module was already processed: " + Module->GetFileName());

        ModuleBldr = new TModuleBuilder(*ModuleDef, *this, {YMake.Conf, Graph, UpdIter, YMake.Commands}, YMake.IncParserManager.Cache);
        ModuleBldr->ProcessMakeFile();
        if (!Module->SelfPeers.empty()) {
            THashSet<TString> ignoreSelfPeers;
            StringSplitter(GetCmdValue(Module->Vars.Get1("_IGNORE_PEERDIRSELF"))).Split(' ').SkipEmpty().Collect(&ignoreSelfPeers);
            for (const auto modId : Module->SelfPeers) {
                const auto mod = YMake.Modules.Get(modId);
                if (!mod || ignoreSelfPeers.contains(mod->GetTag())) {
                    continue;
                }
                const auto depType = Module->GetPeerdirType() == EPT_BuildFrom ? EDT_BuildFrom : EDT_Include;
                AddUniqueDep(depType, mod->GetNodeType(), modId);
            }
        }
        NodeType = Module->GetNodeType();
        return;
    }

    //Y_ASSERT(Deps.empty() || ModuleData.CmdInfo || Deps[0].DepType == EDT_OutTogether);
    const auto& modData = GetModuleData();
    if (modData.CmdInfo != nullptr) {
        TCommandInfo& cmdInfo = *modData.CmdInfo;
        Y_ASSERT(UseFileId(NodeType));
        bool cmdChanged = false;
        if (UpdNode != TNodeId::Invalid) {
            for (const auto& edge : Graph.Get(UpdNode).Edges()) {
                if (*edge == EDT_BuildCommand) {
                    const TDepTreeNode nodeDep = edge.To().Value();
                    const ui32 cmdElemId = ::ElemId(cmdInfo.Cmd.EntryPtr->first);
                    if (nodeDep.ElemId != cmdElemId)
                        cmdChanged = true;
                    break; // there can be only one BuildCommand
                }
                if (cmdChanged)
                    YDIAG(Dev) << "cmdChanged: " << Graph.GetFileName(NodeType, ElemId) << Endl;
            }
            if (cmdInfo.GetCmdType() == TCommandInfo::MacroImplInp) {
                cmdChanged |= CheckInputsChange();
            }
        }
    }
}

bool TNodeAddCtx::CheckInputsChange() const {
    Y_ASSERT(UpdNode != TNodeId::Invalid);
    YDIAG(Dev) << "MacroImplInp, check " << Graph.GetFileName(NodeType, ElemId) << Endl;
    // compare inputs (BuildFrom), too
    TDeps::const_iterator i = Deps.begin(), e = Deps.end();
    for (const auto& edge : Graph.Get(UpdNode).Edges()) {
        if (*edge == EDT_BuildFrom) {
            const TDepTreeNode nodeDep = edge.To().Value();
            while (i != e && i->DepType != EDT_BuildFrom)
                ++i;
            if (i == e || nodeDep.ElemId != i++->ElemId) {
                YDIAG(Dev) << "cmdChanged2: " << Graph.GetFileName(NodeType, ElemId) << Endl;
                return true;
            }
        }
    }
    while (i != e && i->DepType != EDT_BuildFrom)
        ++i;
    if (i != e) {
        YDIAG(Dev) << "cmdChanged3: " << Graph.GetFileName(NodeType, ElemId) << Endl;
        return true;
    }
    return false;
}

TCreateParsedInclsResult TNodeAddCtx::CreateParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) {
    return CreateParsedIncls(Module, Graph, UpdIter, UpdIter.YMake, NodeType, ElemId, type, files);
}

// Note: This method does not add created node to own deps
TCreateParsedInclsResult TNodeAddCtx::CreateParsedIncls(
    TModule* module, TDepGraph& graph, TUpdIter& updIter, TYMake& yMake,
    EMakeNodeType cmdNodeType, ui64 cmdElemId,
    TStringBuf type, const TVector<TResolveFile>& files
) {
    Y_ASSERT(module != nullptr);

    if (files.empty() && type == TStringBuf("*")) {
        return {nullptr, TCreateParsedInclsResult::Nothing};
    }

    YDIAG(IPRP) << "TNodeAddCtx::CreateParsedIncls for " << cmdElemId << " "
                 << graph.ToString(graph.GetNodeById(cmdNodeType, cmdElemId)) << " " << type << Endl;

    //ParsedIncls can be added from cmd-node (if in conf there is "include" option) and from file-node.
    //this property will be added by name that is formed with id of parent node =>
    //if there is a clash by id in FileConf and CommandConf we will have a clash by name and extra deps somewhere (in file or in cmd)
    //so we can modify property value (as unused in general) to avoid clash by name
    TString propName = FormatCmd(cmdElemId, TString::Join("ParsedIncls.", type),
                                 UseFileId(cmdNodeType) ? "" : "cmdOrigin");
    EMakeNodeType propType = EMNT_BuildCommand;
    ui32 propElemId = graph.Names().AddName(propType, propName);
    const auto propCacheId = MakeDepsCacheId(propType, propElemId);
    auto pi = updIter.Nodes.Insert(propCacheId, &yMake, module);
    TCreateParsedInclsResult res = {pi->second.AddCtx, TCreateParsedInclsResult::Existing};
    if (files.empty()) {
        auto dummy = NPath::DummyFile();
        if (res.Node->AddUniqueDep(EDT_Include, FileTypeByRoot(dummy), dummy)) {
            res.Status = TCreateParsedInclsResult::Changed;
        };
    } else {
        for (const auto& file : files) {
            if (!file.Empty()) {
                if (res.Node->AddUniqueDep(EDT_Include, FileTypeByRoot(file.Root()), file.GetElemId())) {
                    res.Status = TCreateParsedInclsResult::Changed;
                }
            }
        }
    }
    if (res.Status == TCreateParsedInclsResult::Changed) {
        pi->second.SetReassemble(true);
        YDIAG(IPRP) << "TNodeAddCtx::CreateParsedIncls reassemble for " << cmdElemId << Endl;
    }
    pi->second.SetOnceEntered(false);
    res.Node->ElemId = propElemId;
    res.Node->NodeType = propType;
    return res;
}

void TNodeAddCtx::AddParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) {
    //FIXME(IGNIETFERRO-1039): structured bindings do not work for msvc here
    auto res = CreateParsedIncls(type, files);
    if (res.Status == TCreateParsedInclsResult::Changed) {
        AddUniqueDep(EDT_Property, res.Node->NodeType, res.Node->ElemId);
    }
}

void TNodeAddCtx::AddDirsToProps(const TDirs& dirs, TStringBuf propName) {
    AddDirsToProps(dirs.SaveAsIds(), propName);
}

void TNodeAddCtx::AddDirsToProps(const TVector<ui32>& dirIds, TStringBuf propName) {
    if (dirIds.empty() && IntentByName(propName.Before('.'), false) == EVI_MaxId) {
        return;
    }
    auto cmdName = FormatCmd(ElemId, propName, "");
    AddDep(EDT_Property, EMNT_BuildCommand, cmdName);
    auto propNodeCacheId = Deps.Back().CacheId();

    auto propNodeIt = UpdIter.Nodes.Insert(propNodeCacheId, &YMake, Module);
    auto& propNodeDelayedDeps = UpdIter.DelayedSearchDirDeps.GetDepsByType(EDT_Search)[propNodeCacheId];

    propNodeIt->second.SetOnceEntered(false);
    propNodeIt->second.Props.ClearValues();
    propNodeIt->second.SetReassemble(true);
    propNodeDelayedDeps.clear();

    for (const auto& dirId : dirIds) {
        propNodeDelayedDeps.Push(dirId);
        YDIAG(GUpd) << "Delaying prop dir dep " << NodeType << " " << Graph.ToString(Graph.GetNodeById(NodeType, ElemId))
                    << " " << (ui64)propNodeCacheId << " -> " << dirId << " "
                    << Graph.GetFileName(dirId) << Endl;
    }
}

void TNodeAddCtx::AddDirsToProps(const TPropsNodeList& props, TStringBuf propName) {
    AddDirsToProps(PropsListToDirsIds(props), propName);
}

void TNodeAddCtx::InitDepsRule() {
    if (!DepsRuleSet) {
        TFileView file = Graph.GetFileName(NodeType, ElemId);
        if (file.GetContextType() != ELT_Action) {
            SetDepsRuleByName(file.GetTargetStr());
        }
    }
}

const TIndDepsRule* TNodeAddCtx::SetDepsRuleByName(TStringBuf name) {
     SetDepsRule(YMake.IncParserManager.IndDepsRuleByPath(name));
     return DepsRule;
}

void TNodeAddCtx::LeaveModule() {
    Y_ASSERT(Module != nullptr);
    YDIAG(GUpd) << "LeaveModule " << ElemId << Endl;
    YDIAG(Dev) << "LeaveModule: " << Module->GetFileName() << Endl;
}

void TNodeAddCtx::NukeModule() {
    Y_ASSERT(Module != nullptr);

    if (IsModule) {
        YDIAG(GUpd) << "NukeModule " << ElemId << Endl;
        YDIAG(Dev) << "NukeModule: " << Module->GetFileName() << Endl;
        if (ModuleDef != nullptr) {
            delete ModuleDef;
            ModuleDef = nullptr;
        }
        if (Module != nullptr) {
            YMake.Modules.Destroy(*Module);
        }
    }
}

void TNodeAddCtx::RemoveIncludeDeps(ui64 startFrom) {
    if (!PartEdit || startFrom >= Deps.Size()) {
        return;
    }
    auto e = std::remove_if(Deps.begin() + startFrom, Deps.end(), [](const TAddDepDescr& d) {
        if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes)
            return d.DepType == EDT_Include && d.NodeType != EMNT_BuildCommand;
        else
            return d.DepType == EDT_Include;
    });
    Deps.Erase(e, Deps.end());
}

TNodeAddCtx::~TNodeAddCtx() {
    if (IsModule && ModuleDef != nullptr) {
        delete ModuleDef;
    }
    GetEntry().AddCtx = nullptr;
}

void TNodeAddCtx::Init2(TAddIterStack& stack, TFileHolder& fileContent, TModule* mod, EReadFileContentMethod readMethod) {
    NeedInit2 = false;
    TDGIterAddable& st = stack.back();
    if (NodeType == EMNT_Deleted) { // parser code can't set this type so using as a flag is OK
        NodeType = st.Node.NodeType;
    }
    ElemId = st.Node.ElemId;
    UpdNode = st.NodeStart;

    Y_ASSERT(UpdNode != TNodeId::Invalid || st.CurDep || Graph.GetNodeById(NodeType, ElemId).Id() == TNodeId::Invalid);
    if (Y_UNLIKELY(Diag()->FU) && UseFileId(NodeType) && UpdNode == TNodeId::Invalid) {
        Cerr << "NEW: " << Graph.Names().FileNameById(ElemId) << Endl;
    }
    if (st.CurDep) {
        UseOldDeps();
        PartEdit = true;
        st.CurDep = 0;
    }
    if (UseFileId(NodeType)) {
        auto fileName = Graph.Names().FileNameById(ElemId);
        if (readMethod == EReadFileContentMethod::FORCED && IsFileType(NodeType)) {
            if (!fileContent) {
                fileContent = Graph.Names().FileConf.GetFileById(fileName.GetElemId());
            }
            if (fileContent->GetName().IsType(NPath::Source) && !fileContent->IsNotFound()) {
                fileContent->UpdateContentHash();
            }
        }
        YMake.Parser->ProcessFile(fileName, *this, stack, fileContent, mod);
    } else {
        auto cmdName = Graph.Names().CmdNameById(ElemId);
        YMake.Parser->ProcessCommand(cmdName, *this, stack);
    }
}

void TNodeAddCtx::WriteHeader() {
    Deps.Lock();
    FlushState->FlushPos = 0;
    if (UpdNode != TNodeId::Invalid) {
        const TDepTreeNode node = Graph.Get(UpdNode).Value();

        // This is desirable property (though not required), so let's debug it
        // FIXME: temporary fix for directories which shouldn't be handled in some cases
        //        another fix for File reparsed as Makefile
        if (NodeType != node.NodeType) {
            YDIAG(Dev) << "WriteHeader: Node type mismatch for "
                        << Graph.GetFileName(node.NodeType, node.ElemId)
                        << " new: " << NodeType << " != old: " << node.NodeType << Endl;
        }
        Y_ASSERT(NodeType == node.NodeType || (IsDirType(NodeType) && IsDirType(node.NodeType)) || (NodeType == EMNT_MakeFile && node.NodeType == EMNT_File) ||
                 (NodeType == EMNT_File && node.NodeType == EMNT_MissingFile));

        AssertEx(IsFileType(NodeType) == IsFileType(node.NodeType), "Inplace node type changes are prohibited");
        AssertEx(ElemId == node.ElemId, "Inplace node renames are prohibited");
        Graph.Get(UpdNode)->NodeType = NodeType;
        FlushState->NodeId = UpdNode;
        Graph.Get(UpdNode).ClearEdges();
    } else {
        FlushState->NodeId = Graph.AddNode(NodeType, ElemId).Id();
    }
    YDIAG(NATR) << ElemId << " -> " << FlushState->NodeId << "\n";
}

TFlushState::TFlushState(TDepGraph& graph, TNodeId oldNodeId, EMakeNodeType newNodeType) {
    if (oldNodeId == TNodeId::Invalid) {
        IsNewNode = true;
        return;
    }

    auto node = graph.Get(oldNodeId);

    if (newNodeType != node->NodeType) {
        IsNewNode = true;
        return;
    }

    CheckEdges_ = ShouldCheckEdgesChanged(newNodeType);
    if (CheckEdges_) {
        for (auto oldEdge : node.Edges()) {
            OldEdges_.emplace(oldEdge.To().Id(), oldEdge.Value());
        }
    }
}

void TFlushState::FinishFlush(TDepGraph& graph, TDepGraph::TNodeRef node) {
    // By default the flush is a sign of a structure change in every node
    // and a sign of a content change in nodes which could be checked for a content change.
    bool structureChanged = true;
    bool contentChanged = ShouldCheckContentChanged(node->NodeType);

    // We can also additionally check already existent nodes whether they really had changes.
    if (!IsNewNode) {
        if (CheckEdges_ && !AreEdgesChanged(node))
            structureChanged = false;

        // Should not flush file node if its content isn't changed.
        // So the flush signs a changed content.
        // TODO: This assert actually triggers. Should investigate.
        // Y_ASSERT(graph.Names().FileConf.GetFileDataById(node->ElemId).Changed);
        // contentChanged = true
        if (ShouldCheckContentChanged(node->NodeType))
            if (!graph.Names().FileConf.GetFileDataById(node->ElemId).Changed)
                contentChanged = false;
    }

    node->State.SetLocalChanges(structureChanged, contentChanged);
}

bool TFlushState::AreEdgesChanged(TDepGraph::TConstNodeRef nodeRef) const {
    auto hasOldEdge = [&](TNodeId toNodeId, EDepType edgeType) {
        return OldEdges_.find(std::make_pair(toNodeId, edgeType)) != OldEdges_.end();
    };

    size_t newEdgesCount = 0;
    for (auto newEdge : nodeRef.Edges()) {
        if (!hasOldEdge(newEdge.To().Id(), newEdge.Value()))
            return true;
        ++newEdgesCount;
    }

    return newEdgesCount != OldEdges_.size();
}

TNodeId TNodeAddCtx::Flush(TAddIterStack& stack, TAutoPtr<TNodeAddCtx>& me, bool lastTry) {
    Y_ASSERT(!FlushDone);

    for (size_t n = 1; n < stack.size() - 1; n++) {
        const auto& item = stack[n];
        const TDepTreeNode& node = item.Node;
        const TUpdEntryPtr& entry = item.EntryPtr;
        if (UseFileId(node.NodeType) == UseFileId(NodeType) && node.ElemId == ElemId && entry && entry->second.AddCtx == me.Get()) {
            Y_UNUSED(me.Release()); // Flush will be performed deeper on stack
            return TNodeId::Invalid;
        }
    }

    YDIAG(Dev) << "Flush node " << (UseFileId(NodeType) ?
        Graph.GetFileName(NodeType, ElemId).GetTargetStr() : Graph.GetCmdName(NodeType, ElemId).GetStr()) << Endl;

    if (!FlushState) {
        FlushState.Reset(new TFlushState{Graph, UpdNode, NodeType});
        WriteHeader();
    }

    if (UseFileId(NodeType)) {
        TNodeData& nodeData = Graph.GetFileNodeData(ElemId);
        nodeData.NodeModStamp = YMake.TimeStamps.CurStamp();
    }

    auto nodeRef = Graph.GetNodeById(NodeType, ElemId);

    for (auto depsIterator = Deps.begin() + FlushState->FlushPos; depsIterator != Deps.end(); ++depsIterator) {
        const TAddDepDescr& dep = *depsIterator;
        if (IsDepDeleted(dep) || (IsSearchDirDep(NodeType, dep.DepType, dep.NodeType))) {
            continue;
        }
        TNodeId depNodeId = Graph.GetNodeById(dep.NodeType, dep.ElemId).Id();

        if (Y_UNLIKELY(depNodeId == TNodeId::Invalid)) {
            size_t positionInStack = FindInStack(stack, dep.NodeType, dep.ElemId);
            if (!positionInStack) {
                auto relocationIt = YMake.Parser->RelocatedNodes.find(MakeDepsCacheId(dep.NodeType, dep.ElemId));
                if (relocationIt != YMake.Parser->RelocatedNodes.end()) {
                    ui64 newId = relocationIt->second.ElemId;
                    EMakeNodeType newType = relocationIt->second.NodeType;
                    depNodeId = Graph.GetNodeById(newType, newId).Id();
                    if (depNodeId != TNodeId::Invalid) {
                        nodeRef.AddEdge(depNodeId, dep.DepType);
                        continue;
                    }

                    positionInStack = FindInStack(stack, newType, newId);
                    Y_ASSERT(positionInStack);
                } else if (lastTry) {
                    depNodeId = Graph.AddNode(dep.NodeType, dep.ElemId).Id();
                    if (depNodeId != TNodeId::Invalid) {
                        nodeRef.AddEdge(depNodeId, dep.DepType);
                        continue;
                    }
                } else if (stack.back().IsInducedDep) {
                    // Flush InducedDep node along with its owning node
                    positionInStack = stack.size() - 2;
                }
            }
            FlushState->FlushPos = depsIterator - Deps.begin();

            stack[positionInStack].RegisterDelayed(me);
            return FlushState->NodeId;
        }

        Y_ASSERT(depNodeId != TNodeId::Invalid);
        nodeRef.AddEdge(depNodeId, dep.DepType);
    }

    FlushState->FinishFlush(Graph, nodeRef);
    TNodeId nodeId = FlushState->NodeId;
    FlushState.Destroy();
    FlushDone = true;
    return nodeId;
}

bool TNodeAddCtx::IsDepDeleted(const TAddDepDescr& dep) const {
    if (dep.DepType != EDT_Property || dep.NodeType != EMNT_Property) {
        return false;
    }
    TStringBuf propName = GetPropertyName(Graph.GetCmdName(dep.NodeType, dep.ElemId).GetStr());
    return propName == "DELETED";
}

void TNodeAddCtx::DeleteDep(size_t idx) {
    const auto& oldDep = Deps[idx];
    TStringBuf oldName = Graph.GetCmdName(oldDep.NodeType, oldDep.ElemId).GetStr();
    auto propId = Graph.Names().AddName(EMNT_Property, FormatProperty("DELETED", oldName));
    Deps.Replace(idx, EDT_Property, EMNT_Property, propId);
}

void TNodeAddCtx::UpdCmdStamp(TNameDataStore<TCommandData, TCmdView>& conf, TTimeStamps& stamps, bool changed) {
    auto& data = conf.GetById(ElemId);
    if (UpdNode == TNodeId::Invalid || changed) {
        data.CmdModStamp = stamps.CurStamp();
    }
}

void TNodeAddCtx::UpdCmdStampForNewCmdNode(TNameDataStore<TCommandData, TCmdView>& conf, TTimeStamps& stamps, bool changed) {
    auto& data = conf.GetById(TVersionedCmdId(ElemId).CmdId());
    if (UpdNode == TNodeId::Invalid || changed) {
        data.CmdModStamp = stamps.CurStamp();
    }
}

bool TMaybeNodeUpdater::AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) {
    if (Deps.Push({depType, elemNodeType, elemId})) {
        SavedRequests.push_back(ERequestType::SingleDep);
    }
    return true;
}

bool TMaybeNodeUpdater::AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) {
    AddUniqueDep(depType, elemNodeType, Names.AddName(elemNodeType, elemName));
    return true;
}

bool TMaybeNodeUpdater::HasAnyDeps() const {
    return !Deps.empty();
}

void TMaybeNodeUpdater::AddParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) {
    ParsedIncls.push_back({TString(type), files});
    SavedRequests.push_back(ERequestType::Includes);
}

void TMaybeNodeUpdater::AddDirsToProps(const TDirs& dirs, TStringBuf propName) {
    AddDirsToProps(dirs.SaveAsIds(), propName);
}

void TMaybeNodeUpdater::AddDirsToProps(const TVector<ui32>& dirIds, TStringBuf propName) {
    ParsedDirs.push_back({TString(propName), dirIds});
    SavedRequests.push_back(ERequestType::Dirs);
}

void TMaybeNodeUpdater::AddDirsToProps(const TPropsNodeList& props, TStringBuf propName) {
    AddDirsToProps(PropsListToDirsIds(props), propName);
}

bool TMaybeNodeUpdater::HasChangesInDeps(TConstDepNodeRef otherNode) const {
    size_t depsCount = 0;
    for (const auto& dep : otherNode.Edges()) {
        // We should ignore order of the deps because new deps will be sorted
        // after graph constructions, and old deps already are.
        if (IsPropertyDep(dep)) {
            // Do not consider Properties for comparision (???)
        } else if (otherNode->NodeType == EMNT_NonParsedFile && EqualToOneOf(dep.Value(), EDT_BuildFrom, EDT_BuildCommand, EDT_OutTogether, EDT_OutTogetherBack)) {
            // Do not consider BuildFrom and BuildCommand edges for generated files
        } else {
            depsCount++;
            if (!Deps.has(TAddDepDescr{dep.Value(), dep.To()->NodeType, dep.To()->ElemId})) {
                // Try to respect InclFixer changes to avoid false positive results.
                if (IsIncludeFileDep(dep)) {
                    Y_ASSERT(UseFileId(dep.To()->NodeType));
                    TFileView name = Names.FileNameById(dep.To()->ElemId);
                    if (name.IsType(NPath::Source)) {
                        auto unsetElemId = Names.FileConf.GetIdNx(NPath::SetType(name.GetTargetStr(), NPath::Unset));
                        auto isFixedInclude = Deps.has(TAddDepDescr{dep.Value(), EMNT_MissingFile, unsetElemId});
                        if (isFixedInclude) {
                            continue;
                        }
                    }
                }
                return true;
            }
        }
    }
    return depsCount != Deps.size();
}

void TMaybeNodeUpdater::UpdateNode(TAddDepAdaptor& node) const {
    auto depsIt = Deps.begin();
    auto inclsIt = ParsedIncls.begin();
    auto dirsIt = ParsedDirs.begin();

    for (const auto& requestType : SavedRequests) {
        switch (requestType) {
            case ERequestType::SingleDep: {
                const auto& dep = *depsIt++;
                node.AddUniqueDep(dep.DepType, dep.NodeType, dep.ElemId);
                break;
            }
            case ERequestType::Includes: {
                const auto& includes = *inclsIt++;
                node.AddParsedIncls(includes.Type, includes.Includes);
                break;
            }
            case ERequestType::Dirs: {
                const auto& dirs = *dirsIt++;
                node.AddDirsToProps(dirs.Dirs, dirs.Type);
                break;
            }
        }
    }
}
