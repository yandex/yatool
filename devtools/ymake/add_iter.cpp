#include "add_node_context_inline.h"

#include "add_iter.h"
#include "add_iter_debug.h"
#include "module_loader.h"
#include "module_dir.h"
#include "module_builder.h"
#include "module_state.h"
#include "prop_names.h"
#include "ymake.h"

#include <devtools/ymake/module_state.h>
#include <devtools/ymake/common/string.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/progress_manager.h>
#include <devtools/ymake/diag/trace.h>

#include <devtools/ymake/symbols/globs.h>

#include <util/generic/algorithm.h>
#include <util/generic/scope.h>
#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/string/split.h>

namespace {
    ui32 GetNeverCachePropElem(const TDepGraph& graph) {
        auto node = graph.GetCommandNode(NProps::NEVERCACHE_PROP);
        return node.IsValid() ? node->ElemId : 0;
    }

    struct GlobNodeInfo {
        TStringBuf GlobHash;
        TUniqVector<ui32> WatchDirs;
        TUniqVector<ui32> Refferers;
        TUniqVector<ui32> Excludes;
        TExcludeMatcher ExcludesMatcher;
    };

    GlobNodeInfo ExtractGlobInfo(const TDGIterAddable& st, TFileView dirName) {
        GlobNodeInfo res;
        const auto node = st.Graph.Get(st.NodeStart);
        for (const auto& edge : node.Edges()) {
            if (edge.Value() == EDT_Property && edge.To()->NodeType == EMNT_Property) {
                const auto prop = st.Graph.GetCmdName(edge.To()).GetStr();
                const auto propName = GetPropertyName(prop);
                if (propName == NProps::GLOB_HASH) {
                    res.GlobHash = GetPropertyValue(prop);
                } else if (propName == NProps::REFERENCED_BY) {
                    res.Refferers.Push(edge.To()->ElemId);
                } else if (propName == NProps::GLOB_EXCLUDE) {
                    if (res.Excludes.Push(edge.To()->ElemId)) {
                        res.ExcludesMatcher.AddExcludePattern(dirName, GetPropertyValue(prop));
                    }
                }
            } else if (edge.Value() == EDT_Search && IsDirType(edge.To()->NodeType)) {
                res.WatchDirs.Push(edge.To()->ElemId);
            }
        }
        return res;
    }

    void UpdateGlobNode(TUpdIter& updIter, TDGIterAddable& st, TStringBuf pattern, TFileView dirName) {
        auto globInfo = ExtractGlobInfo(st, dirName);
        if (!TGlob::WatchDirsUpdated(st.Graph.Names().FileConf, globInfo.WatchDirs)) {
            return;
        }

        try {
            TGlob glob{st.Graph.Names().FileConf, pattern, dirName};
            const auto matchedFiles = glob.Apply(globInfo.ExcludesMatcher);
            if (glob.GetMatchesHash() == globInfo.GlobHash && glob.GetWatchDirs().Data() == globInfo.WatchDirs.Data()) {
                return;
            }

            st.ForceReassemble();
            st.StartEdit(updIter.YMake, updIter);
            TUniqVector<ui32> matchIds;
            for (auto match: matchedFiles) {
                matchIds.Push(st.Graph.Names().FileConf.ConstructLink(ELinkType::ELT_Text, match).GetElemId());
            }
            const auto globHash = st.Graph.Names().AddName(EMNT_Property, FormatProperty(NProps::GLOB_HASH, glob.GetMatchesHash()));
            TModuleGlobInfo globModuleInfo{static_cast<ui32>(st.Add->ElemId), globHash, glob.GetWatchDirs().Data(), matchIds.Take(), globInfo.Excludes.Data(), 0};
            PopulateGlobNode(*st.Add, globModuleInfo);
            for (ui32 reffererId: globInfo.Refferers.Data()) {
                st.Add->AddDep(EDT_Property, EMNT_Property, reffererId);
            }
        } catch (const yexception&) {
            // Invalid pattern error was reported earlier
        }
    }

    bool GlobNeedUpdate(const TDGIterAddable& st, TStringBuf pattern, TFileView dirName) {
        auto globInfo = ExtractGlobInfo(st, dirName);
        if (!TGlob::WatchDirsUpdated(st.Graph.Names().FileConf, globInfo.WatchDirs)) {
            return false;
        }

        bool result;
        try {
            TGlob glob{st.Graph.Names().FileConf, dirName, pattern, globInfo.GlobHash, std::move(globInfo.WatchDirs)};
            result = glob.NeedUpdate(globInfo.ExcludesMatcher);
        } catch (const yexception&) {
            result = false;
        }
        return result;
    }

    NEvent::TForeignPlatformTarget PossibleForeignPlatformEvent(TFileView modDir, NEvent::TForeignPlatformTarget::EPlatform platform) {
        Y_ASSERT(modDir.InSrcDir() && !modDir.IsLink());
        NEvent::TForeignPlatformTarget res;
        res.SetPlatform(platform);
        res.SetReachable(::NEvent::TForeignPlatformTarget::POSSIBLE);
        res.SetDir(TString{modDir.CutType()});
        return res;
    }
}

TModuleResolveContext MakeModuleResolveContext(const TModule& mod, const TRootsOptions& conf, TDepGraph& graph, const TUpdIter& updIter,
                                               const TParsersCache& parsersCache) {
    NGraphUpdater::TNodeStatusChecker nodeStatusChecker = [&updIter](const TDepTreeNode& node) {
        return updIter.CheckNodeStatus(node);
    };
    return TModuleResolveContext(conf, graph, mod.GetSharedEntries(), parsersCache,
                                 updIter.ResolveCaches.Get(mod.GetId()), updIter.ResolveStats, nodeStatusChecker);
}

void TDelayedSearchDirDeps::Flush(const TGeneralParser& parser, TDepGraph& graph) const {

    auto GetNodeType = [&parser, &graph](EDepType depType, ui32 depNodeId) {
        if (depType == EDT_Search) {
            return parser.DirectoryType(graph.GetFileName(depNodeId));
        }
        if (depType == EDT_Include) {
            return EMNT_NonParsedFile;
        }
        yexception() << "Dep type " << depType << " not supported for delayed nodes" << Endl;
        return EMNT_Deleted;
    };

    for (const auto& [depType, nodeToDeps] : DepTypeToNodeDeps) {
        for (const auto& [cacheId, delayedDeps] : nodeToDeps) {
            const auto elemId = ElemId(cacheId);
            auto node = IsFile(cacheId) ? graph.GetFileNodeById(elemId) : graph.GetCommandNodeById(elemId);

            Y_ENSURE(node.IsValid() || delayedDeps.empty());

            for (const auto& depNodeId : delayedDeps) {
                const auto depNodeType = GetNodeType(depType, depNodeId);

                auto existingDepNode = graph.GetNodeById(depNodeType, depNodeId);
                auto depNode = existingDepNode.IsValid() ? existingDepNode : graph.AddNode(depNodeType, depNodeId);
                depNode->NodeType = depNodeType;
                node.AddUniqueEdge(depNode, depType);
            }
        }
    }
}

TUpdEntryStats::TUpdEntryStats(bool inStack, const TNodeDebugOnly& nodeDebug)
    : TNodeDebugOnly(nodeDebug)
    , AllFlags(0)
    , Props(nodeDebug)
{
    InStack = inStack;
}

void TUpdEntryStats::DepsToInducedProps(TPropertyType propType, const TDeps& deps, TPropertySourceDebugOnly sourceDebug) {
    TVector<TDepsCacheId> props;
    props.reserve(deps.Size());
    for (const auto& d : deps)
        props.push_back(MakeDepsCacheId(d.NodeType, d.ElemId));
    Props.AddValues(propType, props, sourceDebug);
}

TUsingRules TUpdEntryStats::GetRestrictedProps(EDepType depType, TUsingRules propsToUse) const {
    if (depType != EDT_OutTogether && depType != EDT_OutTogetherBack) {
        return propsToUse;
    }
    return {};
}

void ReadDeps(const TDepGraph& graph, TNodeId nodeId, TDeps& deps) {
    const TDepGraph::TConstNodeRef node = graph.Get(nodeId);
    deps.Reserve(node.Edges().Total());
    for (const auto& edge : node.Edges()) {
        deps.Add(edge.Value(), edge.To()->NodeType, edge.To()->ElemId);
    }
}

bool TUpdIterStBase::NodeToProps(TDepGraph& graph, TDelayedSearchDirDeps& delayedDirs, const TNodeAddCtx* addCtx) {
    Y_ASSERT(!UseFileId(Node.NodeType));
    auto versionedName = graph.GetCmdName(Node);
    if (versionedName.IsNewFormat()) {
        // not supported
        return true;
    }
    TStringBuf name = versionedName.GetStr();
    TStringBuf fullPropName = Node.NodeType == EMNT_Property ? GetPropertyName(name) : GetCmdName(name);
    TStringBuf intentName, propName;
    fullPropName.Split('.', intentName, propName);
    if (!propName) {
        // ignore properties whose names are not in a form "intent.property_type"
        return true;
    }

    TUpdEntryStats& entry = EntryPtr->second;
    EVisitIntent intentId = IntentByName(intentName, false);
    if (intentId == EVI_MaxId) {
        return true;
    }
    const auto& stamp = graph.Names().CommandConf.GetById(Node.ElemId).CmdModStamp;
    entry.Props.UpdateIntentTimestamp(intentId, stamp);
    bool useAddCtx = addCtx && !addCtx->NeedInit2;
    YDIAG(IPRP) << "NodeToProps for " << name
                << " intentId=" << ui32(intentId)
                << " FetchIntent=" << DumpFetchIntents()
                << " timestamp= " << int(stamp)
                << " useAddCtx=" << useAddCtx << Endl;
    if (!ShouldFetchIntent(intentId) && !useAddCtx) {
        if (GetNonRuntimeIntents().Has(intentId)) {
            entry.Props.SetIntentNotReady(intentId, 0, TPropertiesState::ENotReadyLocation::NodeToProps);
            return false;
        }
    }
    TPropertyType propType(graph.Names(), intentId, propName);
    if (Node.NodeType == EMNT_Property) {
        TPropertySourceDebugOnly sourceDebug{Node, EPropertyAdditionType::FromNode};
        TStringBuf val = GetPropertyValue(name);
        if (val.size() && val != TStringBuf("1")) { // TODO: do not write "1" there and remove this check
            TVector<TDepsCacheId> props;
            props.push_back(MakeDepsCacheId(EMNT_Property, Node.ElemId));
            entry.Props.AddValues(propType, props, sourceDebug);
        } else {
            entry.Props.AddSimpleValue(propType, sourceDebug);
        }
    } else if (useAddCtx) {
        TPropertySourceDebugOnly sourceDebug{MakeDepsCacheId(addCtx->NodeType, addCtx->ElemId), EPropertyAdditionType::FromNode};
        entry.DepsToInducedProps(propType, addCtx->Deps, sourceDebug);

        TVector<TDepsCacheId> props;
        auto& searchDirDeps = delayedDirs.GetNodeDepsByType({addCtx->NodeType, static_cast<ui32>(addCtx->ElemId)}, EDT_Search);
        for (const auto& dirId : searchDirDeps) {
            props.push_back(MakeDepsCacheId(EMNT_Directory, dirId));
        }
        entry.Props.AddValues(propType, props, sourceDebug);
    } else {
        TPropertySourceDebugOnly sourceDebug{*graph.Get(NodeStart), EPropertyAdditionType::FromNode};
        TDeps deps;
        ReadDeps(graph, NodeStart, deps);
        entry.DepsToInducedProps(propType, deps, sourceDebug);
    }

    return true;
}

TDGIterAddable::TDGIterAddable(TDepGraph& graph, TNodeId nodeId)
    : TUpdIterStBase(graph, nodeId)
    , Graph(graph)
{
    NodeStart = nodeId;
    Node = graph.Get(NodeStart).Value();
    YDIAG(GUpd) << "new IterAddable " << graph.ToString(Node) << " " << Node.ElemId << " " << Node.NodeType << Endl;
}

TDGIterAddable::TDGIterAddable(TDepGraph& graph, const TAddDepIter& it)
    : TUpdIterStBase(graph, it.DepNodeId)
    , Graph(graph)
{
    NodeStart = it.DepNodeId != TNodeId::Invalid ? it.DepNodeId : graph.GetNodeById(it.DepNode).Id();
    if (NodeStart != TNodeId::Invalid) {
        Node = graph.Get(NodeStart).Value();
    } else {
        Node = it.DepNode;
    }
#if defined(YMAKE_DEBUG)
    DebugNode = MakeDepsCacheId(Node.NodeType, Node.ElemId);
#endif
    YDIAG(GUpd) << "new IterAddable " << graph.ToString(Node) << " " << Node.ElemId << " " << Node.NodeType << Endl;
}

TDGIterAddable::~TDGIterAddable() {
    Y_ASSERT(DelayedNodes.empty() || UncaughtException());
}

TModule* TDGIterAddable::EnterModule(TYMake& yMake) {
    if (Add && Add->IsModule) {
        YDIAG(Dev) << "EnterModule: " << Add->Module->GetFileName() << Endl;
        return Add->Module;
    }

    auto module = RestoreModule(yMake);

    TFileView name = yMake.Graph.GetFileName(Node);
    TFileView modName = module->GetName();
    if (name != modName) {
        ythrow TNotImplemented() << "EnterModule: Module name mismatch: " << modName << " instead of " << name;
    }
    YDIAG(Dev) << "EnterModule: " << modName << Endl;
    return GetAddCtx(module, yMake, true).Module;
}

TModule* TDGIterAddable::RestoreModule(TYMake& yMake) const {
    const TConstDepNodeRef graphNode = yMake.Graph.GetNodeById(Node.NodeType, Node.ElemId);
    Y_ASSERT(graphNode.IsValid());
    TModuleRestorer restorer(yMake.GetRestoreContext(), graphNode);
    return restorer.RestoreModule();
}

void TUpdIter::SaveModule(ui64 elemId, const TAutoPtr<TModuleDef>& modDef) {
    TNodes::iterator i = Nodes.Insert(MakeDepFileCacheId(elemId), &YMake, &modDef->GetModule());
    if (i->second.HasModule()) {
        Y_UNUSED(modDef.Release());
        ythrow TError() << "More than 1 module of the same name " << Graph.GetFileName(elemId) << " in graph";
    }
    i->second.AddCtx->IsModule = true;
    i->second.AddCtx->ModuleDef = modDef.Get();
    YDIAG(GUpd) << "SaveModule: " << Graph.GetFileName(elemId) << " (" << elemId << ")" << Endl;
    Y_UNUSED(modDef.Release());
    i->second.SetReassemble(true);
}

/// This function decides whether node belongs to module (or is module)
/// The basic idea is that directories and makefiles along with their properties do not belong to any module.
inline bool TDGIterAddable::NeedModule(const TAddIterStack& stack) const {
    if (stack.size() >= 3) {
        const auto& pfrom = stack[stack.size() - 3];
        if (IsNonmodulePropertyDep(pfrom)) {
            // Property values for directories and makefiles don't belong to module
            return false;
        } else if (pfrom.Dep.DepType == EDT_Property) {
            // Other properties do
            return true;
        }
    }
    if (stack.size() >= 2) {
        const auto& from = stack[stack.size() - 2];
        if (IsNonmodulePropertyDep(from)) {
            // Property nodes for directories and makefiles don't belong to module
            return false;
        }
        if (IsMakeFileIncludeDep(from.Node.NodeType, from.Dep.DepType, Node.NodeType)) {
            return false;
        }
        if (from.Dep.DepType != EDT_Include && from.Dep.DepType != EDT_BuildFrom) {
            // Any edges except Include and BuildFrom don't cross module boundary
            return true;
        }
   }
   return !(IsDirType(Node.NodeType) || Node.NodeType == EMNT_MakeFile);
}

inline TModule* TDGIterAddable::GetModuleForEdit(TDepGraph& graph, TUpdIter& dgIter, bool& isModule) {
    TAddIterStack& stack = dgIter.State;
    Y_ASSERT(!stack.empty());
    isModule = IsModuleType(Node.NodeType);
    if (!isModule && !NeedModule(stack)) {
        return nullptr;
    }
    YDIAG(GUpd) << Node.ElemId << ": name = " << graph.ToString(Node) << ", modPos = " << ModulePosition << " (ElemId = "
                << (ModulePosition ? stack[ModulePosition].Node.ElemId : 0) << "), isMod = " << isModule << Endl;
    if (!isModule) {
        if (ModulePosition) {
            if (stack[ModulePosition].Add.Get()) {
                return stack[ModulePosition].Add->Module;
            }
        } else {
            AssertEx(dgIter.ParentModule != nullptr, "Parent module missing");
            return dgIter.ParentModule;
        }
    }
    TDGIterAddable& stMod = stack[ModulePosition];
    return stMod.EnterModule(dgIter.YMake);
}

TModuleBuilder* TDGIterAddable::GetParentModuleBuilder(TUpdIter& dgIter) {
    if (!NeedModule(dgIter.State)) {
        return nullptr;
    }

    if (!ModulePosition) {
        // in that case parent module is TUpdIter::ParentModule,
        // which is TModules::RootModule,
        // which doesn't have AddCtx (not present in graph at all)
        return nullptr;
    }

    if (!dgIter.State[ModulePosition].Add) {
        // we can make new AddCtx using
        // `GetAddCtx(dgIter.State[ModulePosition].RestoreModule(dgIter.YMake), dgIter.YMake, true);`
        // like in TDGIterAddable::EnterModule,
        // but anyway it would not contain ModuleBldr
        return nullptr;
    }

    return dgIter.State[ModulePosition].Add->ModuleBldr;
}

static inline bool IsReassemblingDirectory(const TDGIterAddable* parent) {
    if (!parent) {
        return false;
    }
    return parent->Node.NodeType == EMNT_Directory && parent->Entry().Reassemble;
}

bool TDGIterAddable::NeedUpdate(TYMake& yMake, TFileHolder& fileContent, bool reassemble, TDGIterAddable& lastInStack, const TDGIterAddable* prevInStack, const TDGIterAddable* pprevInStack) {
    switch (Node.NodeType) {
        case EMNT_MakeFile: {
            if (IsReassemblingDirectory(prevInStack) || ShouldFetchIntent(EVI_GetModules)) {
                return true;
            } else {
                Entry().Props.SetIntentNotReady(EVI_GetModules, TPropertiesState::ENotReadyLocation::NeedUpdate);
            }
            [[fallthrough]];
        }
        case EMNT_MissingFile:
            if (!reassemble && pprevInStack && pprevInStack->Dep.DepType == EDT_Property &&
                (prevInStack->Node.NodeType == EMNT_BuildCommand || prevInStack->Node.NodeType == EMNT_Property)) {
                // Don't bother updating missing files in properties without reassemble
                // They are already applied as actual nodes
                YDIAG(V) << "Skipped update: " << yMake.Graph.GetCmdName(Node) << Endl;
                return false;
            }
            [[fallthrough]];
        case EMNT_File:
        case EMNT_NonProjDir:
            return yMake.Parser->NeedUpdateFile(Node.ElemId, Node.NodeType, fileContent);
        case EMNT_MissingDir: {
            Y_ASSERT(UseFileId(Node.NodeType));
            TFileView name = yMake.Graph.GetFileName(Node.ElemId);
            auto& fileConf = yMake.Names.FileConf;
            if (name.IsType(NPath::Build)) {
                return false;
            } else if (name.IsType(NPath::Unset)) {
                return true;
            }
            if (fileConf.YPathExists(name, EPathKind::Dir)) {
                fileConf.GetFileDataById(Node.ElemId).NotFound = false;
                lastInStack.EntryPtr->second.HasChanges = true;
                lastInStack.EntryPtr->second.SetReassemble(true);
                return true;
            }
            return name.IsType(NPath::Source);
        }
        case EMNT_Directory: {
            Y_ASSERT(UseFileId(Node.NodeType));
            TFileView name = yMake.Graph.GetFileName(Node.ElemId);
            if (reassemble) {
                return true;
            }

            const auto actualDirType = yMake.Parser->DirectoryType(name);

            if (actualDirType == EMNT_MissingDir) {
                yMake.Names.FileConf.GetFileDataById(Node.ElemId).NotFound = true;
                ui32 elemId;
                TStringBuf nameStr;
                if (prevInStack) {
                    const auto& node = prevInStack->Node;
                    elemId = node.ElemId;
                    nameStr = UseFileId(node.NodeType) ? yMake.Graph.GetFileName(node).GetTargetStr() : yMake.Graph.GetCmdName(node).GetStr();
                } else {
                    elemId = 0;
                    nameStr = TDiagCtrl::TWhere::TOP_LEVEL;
                }
                TScopedContext context(elemId, nameStr);
                YConfErr(BadDir) << "reference to missing dir " << "[[imp]]" << name.CutType() << "[[rst]]" << Endl;
            }

            if (actualDirType == EMNT_Directory && !Graph.GetFileNode(MakefileNodeNameForDir(yMake.Names.FileConf, name)).IsValid()) {
                return true; // the directory wasn't processed before
            }

            return actualDirType != EMNT_Directory;
        }
        case EMNT_NonParsedFile: //IsOutputType(type)
        case EMNT_Program:
        case EMNT_Library:
        case EMNT_Bundle:
            return reassemble;
        case EMNT_BuildCommand:
        case EMNT_UnknownCommand:
            // NOTE: CmdOrigin is non-null only when parent node is also edited in a module
            return reassemble;
        default:
            break;
    }
    return false;
}

class IPropertiesWrapper {
public:
    virtual ~IPropertiesWrapper() = default;
    virtual const TVector<TDepsCacheId>* FindValues(TPropertyType type) const = 0;
    virtual bool HasProperty(TPropertyType type) const = 0;
};

class TPropertiesStateWrapper: public IPropertiesWrapper {
public:
    TPropertiesStateWrapper(const TPropertiesState& props, TUsingRules propsToUse, TUsingRules restrictedProps)
        : Props_(props)
        , PropsToUse_(propsToUse)
        , RestrictedProps_(restrictedProps)
    {
    }

    const TVector<TDepsCacheId>* FindValues(TPropertyType type) const override {
        if (RestrictedProps_.Defined() && RestrictedProps_->get().contains(type)) {
            return nullptr;
        }
        auto values = Props_.FindValues(type);
        return values ? &values->Data() : nullptr;
    }

    bool HasProperty(TPropertyType type) const override {
        return PropsToUse_.Defined() && PropsToUse_->get().contains(type);
    }
private:
    const TPropertiesState& Props_;
    TUsingRules PropsToUse_;
    TUsingRules RestrictedProps_;
};

class TPropertiesCacheWrapper: public IPropertiesWrapper {
public:
    TPropertiesCacheWrapper(const TRawIncludesInfo& info)
        : RawIncludesInfo_(info)
    {
    }

    const TVector<TDepsCacheId>* FindValues(TPropertyType type) const override {
        if (auto ptr = RawIncludesInfo_.FindPtr(type); ptr && !ptr->empty()) {
            return &ptr->Data();
        }
        return nullptr;
    }

    bool HasProperty(TPropertyType /* type */) const override {
        return true;
    }
private:
    const TRawIncludesInfo& RawIncludesInfo_;
};

inline void InduceDeps(TAddDepContext& ctx, const IPropertiesWrapper& iprops);

void TDGIterAddable::RepeatResolving(TYMake& ymake, TAddIterStack& stack, TFileHolder& fileContent) {
    YDIAG(VV) << "Repeat resolving for " << Graph.GetFileName(Node.ElemId) << Endl;
    Entry().OnceProcessedAsFile = true;

    TModule* module = nullptr;
    if (ModulePosition && stack[ModulePosition].Add) {
        module = stack[ModulePosition].Add->Module;
    } else if (ModulePosition) {
        module = stack[ModulePosition].RestoreModule(ymake);
    } else {
        module = ymake.UpdIter->ParentModule;
    }

    TMaybeNodeUpdater node(ymake.Names);
    if (Node.NodeType == EMNT_File) {
        ymake.IncParserManager.ProcessFile(*fileContent, ymake.GetFileProcessContext(module, node));
    } else if (Node.NodeType == EMNT_NonParsedFile) {
        if (auto ptr = module->RawIncludes.FindPtr(Node.ElemId)) {
            node.NodeType = Node.NodeType;
            node.ElemId = Node.ElemId;

            const auto fileName = Graph.GetFileName(Node.NodeType, Node.ElemId).GetTargetStr();
            const auto depRule = ymake.IncParserManager.IndDepsRuleByPath(fileName);
            TAddDepContext nodeCtx(node, *module, Graph, ymake, *ymake.UpdIter, depRule);
            InduceDeps(nodeCtx, TPropertiesCacheWrapper(*ptr));
        } else {
            return;
        }
    }

    if (node.HasChangesInDeps(Graph.GetNodeById(Node))) {
        if (Node.NodeType == EMNT_NonParsedFile) {
            stack[ModulePosition].Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
            YDebug() << "Module " << module->GetName() << " will be reconfigured due to resoving changes for " << Graph.GetFileName(Node.ElemId) << Endl;
            module->RawIncludes.clear();
            return;
        }
        YDIAG(VV) << "Repeat resolving caused changes, file " << Node.ElemId << " was updated" << Endl;
        Add = new TNodeAddCtx(module, ymake, EntryPtr->second, false, nullptr, fileContent, EReadFileContentMethod::ON_DEMAND);
        Add->NeedInit2 = false;
        Add->NodeType = Node.NodeType;
        Add->ElemId = Node.ElemId;
        Add->UpdNode = NodeStart;
        Add->Module = GetModuleForEdit(ymake.Graph, *ymake.UpdIter, Add->IsModule);
        Entry().HasChanges = true;
        Y_ASSERT(Node.NodeType == Add->NodeType);
        if (Node.NodeType == EMNT_File && fileContent->GetName().Extension() == "in") {
            ymake.IncParserManager.ProcessFileWithSubst(*fileContent, ymake.GetFileProcessContext(module, *Add));
        }
        node.UpdateNode(*Add);
    }
}

bool TDGIterAddable::ResolvedInputChanged(TYMake& ymake, TModule& module) {
    YDIAG(VV) << "Repeat resolving command inputs for " << module.GetName() << Endl;

    TModuleResolver resolver{module, ymake.Conf, ymake.GetModuleResolveContext(module)};
    return AnyOf(
        module.ResolveResults,
        [&](const TResolveResult& result) {
            TString origPath;
            ymake.Graph.GetFileName(result.OrigPath).GetStr(origPath);
            if (NPath::IsType(origPath, NPath::Unset)) {
                origPath = TString{NPath::CutType(origPath)};
            }
            auto resolveFile = resolver.ResolveSourcePath(
                origPath,
                result.ResolveDir == TResolveResult::EmptyPath ? TFileView() : ymake.Graph.GetFileName(result.ResolveDir),
                TModuleBuilder::LastTry | TModuleBuilder::Silent
            );
            // resolveFile always must be filled here, because call with LastTry
            auto resultPathId = TResolveResult::EmptyPath == result.ResultPath ? 0 : result.ResultPath;
            auto r = resolveFile.GetElemId() != resultPathId;
            if (r) {
                YDebug() << "Resolved inputs have been changed: " << origPath
                         << " is resolved to " << TResolveFileOut(resolver, resolveFile)
                         << ", but the saved result is " << ymake.Graph.GetFileName(resultPathId)
                         << ". The resolved and the saved results must be the same." << Endl;
            };
            return r;
        }
    );
}

bool TDGIterAddable::ModuleDirsChanged(TYMake& ymake, TModule& module) {
    YDIAG(VV) << "Checking dirs presence for " << module.GetName() << Endl;
    if (module.MissingDirs) {
        for (auto dir : *module.MissingDirs) {
            if (Graph.Names().FileConf.CheckExistentDirectory(dir)) {
                return true;
            }
        }
    }
    for (auto dir : module.SrcDirs) {
        if (Graph.Names().FileConf.CheckLostDirectory(dir)) {
            return true;
        }
    }
    for (const auto& langAddIncls : module.IncDirs.GetAll()) {
        if (langAddIncls.second.LocalUserGlobal) {
            for (auto dir : *langAddIncls.second.LocalUserGlobal) {
                if (ymake.Names.FileConf.CheckLostDirectory(dir)) {
                    return true;
                }
            }
        }
    }
    return false;
}


inline void TDGIterAddable::StartEdit(TYMake& yMake, TUpdIter& dgIter) {
    TAddIterStack& stack = dgIter.State;
    const bool reassemble = Entry().Reassemble;

    TDGIterAddable* prev = dgIter.GetStateByDepth(1);
    TDGIterAddable* pprev = dgIter.GetStateByDepth(2);

    TFileHolder fileContent;

    bool enteredExistentNode = NodeStart != TNodeId::Invalid && !CurDep;
    if (enteredExistentNode) {
        if (!NeedUpdate(yMake, fileContent, reassemble, stack.back(), prev, pprev)) {
            // We perform repeat resolving every ymake's run for correct invalidation
            // of changed addincls or include files with changed statuses.
            if (!IsEdited() && EqualToOneOf(Node.NodeType, EMNT_File, EMNT_NonParsedFile) && !Entry().OnceProcessedAsFile) {
                RepeatResolving(yMake, stack, fileContent);
            }
            BINARY_LOG(Iter, NIter::TStartEditEvent, DebugGraph, Node, IsEdited());
            return;
        }
    }

    Y_ASSERT(!IsEdited());
    Entry().HasChanges = true;
    Entry().OnceProcessedAsFile = true;

    if (!CurDep) {
        // В это место мы попадаем при «первом» входе в узел. «Первых» входов
        // может быть несколько, например, после сброса узла мы также попадём сюда.
        // В любом случае сбрасываем состояние свойств — это либо no-op, либо
        // корректная очистка после сброса узла.
        Entry().Props.ClearValues();
    }

    bool isMod;
    TModule* module = GetModuleForEdit(yMake.Graph, dgIter, isMod);
    if (Y_UNLIKELY(reassemble && Node.NodeType == EMNT_NonParsedFile && !TFileConf::IsLink(Node.ElemId) &&
                  (!module->GetSharedEntries().has(Graph.GetFileNameByCacheId(EntryPtr->first).GetTargetId())))) {
        const TModAddData* modData = dgIter.GetAddedModuleInfo(MakeDepFileCacheId(Node.ElemId));
        if (Y_UNLIKELY(!modData || !modData->Added))  {
            throw TInvalidGraph() << "Generated file " << yMake.Graph.GetFileName(Node) << " entered first from other module " << module->GetName() << " (check your PEERDIR's)";
        }
    }

    if (!Add) {
        Add = new TNodeAddCtx(module, yMake, Entry(), isMod, &stack, fileContent, EReadFileContentMethod::FORCED);
    } else {
        Add->Init2(stack, fileContent, module, EReadFileContentMethod::FORCED);
    }
    Node.NodeType = Add->NodeType; // might have changed
    BINARY_LOG(Iter, NIter::TStartEditEvent, DebugGraph, Node, IsEdited());
    return;
}

inline EMakeNodeType GetFileNodeType(NPath::ERoot root) {
    EMakeNodeType elemNodeType = EMNT_File;
    switch (root) {
        case NPath::Build:
            elemNodeType = EMNT_NonParsedFile;
            break;
        case NPath::Unset:
            elemNodeType = EMNT_MissingFile;
            break;
        default:
            break;
    }
    return elemNodeType;
}

inline void AddInducedDepsToNode(TAddDepContext& ctx, const TVector<TDepsCacheId>& what) {
    TAddDepAdaptor& to = ctx.Node;
    const auto dummyFileElemId = ctx.Graph.Names().FileConf.DummyFile().GetElemId();
    TModule& module = ctx.Module;
    TModuleWrapper wrapper(module, ctx.YMake.Conf, ctx.YMake.GetModuleResolveContext(module));
    TFileView srcFile = ctx.Graph.GetFileName(to.ElemId);
    TVector<TResolveFile> resolved;
    for (auto propNode : what) {
        const auto elemId = ElemId(propNode);
        const TFileView elemName = ctx.Graph.GetFileName(elemId);
        if (elemId == dummyFileElemId) {
            YDIAG(IPRP) << "    [skip] " << elemName << Endl;
        } else {
            resolved.clear();
            wrapper.ResolveSingleInclude(srcFile, elemName.GetTargetStr(), resolved);
            if (resolved.empty()) {
                YDIAG(IPRP) << "    [skip] " << elemName << " (sysincl resolved to nothing)" << Endl;
            } else {
                for (const auto& incFile : resolved) {
                    YDIAG(IPRP) << "    " << elemName << " (resolved to " << TResolveFileOut(wrapper, incFile) << ")" << Endl;
                    to.AddUniqueDep(EDT_Include, FileTypeByRoot(incFile.Root()), incFile.GetElemId());
                }
            }
        }
    }
}

inline void InduceDeps(TAddDepContext& ctx, const IPropertiesWrapper& iprops) {
    TAddDepAdaptor& to = ctx.Node;
    TModule& module = ctx.Module;

    TPropertyType outputIncludePropType{ctx.Graph.Names(), EVI_InducedDeps, "*"};
    if (const auto* outputIncludes = iprops.FindValues(outputIncludePropType)) {
        Y_ASSERT(UseFileId(to.NodeType));
        YDIAG(IPRP) << "Inducing OUTPUT_INCLUDE to " << to.NodeType << " " <<
            ctx.Graph.GetFileName(to.ElemId) << ":" << Endl;

        // 1. Process OUTPUT_INCLUDES via include processor
        TFileView srcFile = ctx.Graph.GetFileName(to.ElemId);
        TVector<TString> includes(Reserve(outputIncludes->size()));
        const auto dummyFileElemId = ctx.Graph.Names().FileConf.DummyFile().GetElemId();
        for (const auto& cacheId : *outputIncludes) {
            TFileView include = ctx.Graph.GetFileNameByCacheId(cacheId);
            // Some resolving is already done in CheckInputs in macro processor
            if (include.GetTargetId() == dummyFileElemId) {
                YDIAG(IPRP) << "    [skip OUTPUT_INCLUDE]" << include << Endl;
            } else if (include.IsType(NPath::Unset)) {
                includes.emplace_back(include.CutType());
            } else {
                TString includeStr;
                include.GetStr(includeStr);
                includes.emplace_back(std::move(includeStr));
            }
        }

        TModuleWrapper wrapper(module, ctx.YMake.Conf, ctx.YMake.GetModuleResolveContext(module));
        auto& parserManager = ctx.YMake.IncParserManager;
        bool parserRes = parserManager.ProcessOutputIncludes(srcFile,
                                                             includes,
                                                             wrapper,
                                                             to,
                                                             ctx.Graph.Names(),
                                                             ctx.UpdIter.State);
        // 2. Fallback to simpler OUTPUT_INCLUDES handling that just adds all
        // deps to current node. We use this approach only
        // when we failed to find include processor that supports OUTPUT_INCLUDES
        // for srcFile in which case ProcessOutputIncludes will return false.
        if (!parserRes) {
            AddInducedDepsToNode(ctx, *outputIncludes);
        }
        Y_ASSERT(iprops.HasProperty(outputIncludePropType));
        if (ctx.UpdateReresolveCache) {
            auto& rawIncludes = module.RawIncludes[to.ElemId][outputIncludePropType];
            for (auto id : *outputIncludes) {
                rawIncludes.Push(id);
            }
        }
    }

    if (!ctx.DepsRule) {
        return;
    }

    const TIndDepsRule& rule = *ctx.DepsRule;
    for (const auto& [type, action] : rule.Actions) {
        if (Diag()->IPRP) {
            YDIAG(IPRP) << "Inducing (" << action << ") " <<  type.GetName(ctx.Graph) <<
                " deps to " << to.NodeType << " " <<
                ctx.Graph.ToString(ctx.Graph.GetNodeById(to.NodeType, to.ElemId)) << ":" << Endl;
        }

        const auto* values = iprops.FindValues(type);
        if (!values) {
            continue;
        }
        if (action == TIndDepsRule::EAction::Use) {
            AddInducedDepsToNode(ctx, *values);
            Y_ASSERT(iprops.HasProperty(type));
            if (ctx.UpdateReresolveCache) {
                auto& rawIncludes = module.RawIncludes[to.ElemId][type];
                for (auto id : *values) {
                    rawIncludes.Push(id);
                }
            }
        }
    }
}

inline void UseByName(TNodeAddCtx& to, const TPropertiesState& iprops, TPropertyType propType, EDepType depType, EMakeNodeType defNodeType) {
    if (const TPropsNodeList* values = iprops.FindValues(propType)) {
        to.AddDepsUnique(*values, depType, defNodeType);
    }
}

inline void AddIntoQueue(TNodesQueue& queue, const TPropertiesState& iprops, TPropertyType propType, const TDepTreeNode& from, EDepType depType, EMakeNodeType toNodeType) {
    const TPropsNodeList* properties = iprops.FindValues(propType);
    if (properties) {
        for (const auto& toCacheId : *properties) {
            queue.AddEdge(from, TDepTreeNode(toNodeType, ElemId(toCacheId)), depType);
        }
    }
}

TIntents TPropertiesIterState::CalcIntentsToReceiveFromChild(
    const TUpdEntryStats& parentState,
    EMakeNodeType prntNodeType,
    EMakeNodeType chldNodeType,
    EDepType edgeType,
    bool mainOutputAsExtra
)
{
    if (mainOutputAsExtra) {
        if (prntNodeType == EMNT_NonParsedFile && edgeType == EDT_OutTogetherBack) {
            return TIntents::None();
        }
    }
    if (edgeType == EDT_Property) {
        if (prntNodeType == EMNT_File) {
            return TIntents{EVI_InducedDeps, EVI_CommandProps, EVI_ModuleProps};
        }
        return TIntents::All();
    }
    switch (prntNodeType) {
        case EMNT_MakeFile:
        case EMNT_BuildCommand:
            return (chldNodeType != EMNT_MakeFile ? TIntents::All() : TIntents::None());
        case EMNT_File:
            if (edgeType == EDT_Include)
                return parentState.PassInducedIncludesThroughFiles
                    ? TIntents{EVI_InducedDeps, EVI_ModuleProps}
                    : TIntents::None();
            return TIntents::All();
        case EMNT_NonParsedFile: { // does not pass EVI_CommandProps
            TIntents result{EVI_InducedDeps, EVI_ModuleProps};
            if (parentState.PassNoInducedDeps)
                result.Remove(EVI_InducedDeps);
            if (edgeType == EDT_Include)
                return parentState.PassInducedIncludesThroughFiles
                    ? result
                    : TIntents::None();
            return result;
        }
        case EMNT_Directory:
        case EMNT_Program: // IsModuleType
        case EMNT_Library:
        case EMNT_Bundle:
        default:
            return TIntents::None();
    }
}

TString DumpProps(const TDepGraph& graph, const TNodeProperties& props) {
    TStringStream out;
    TDelimiter intentDelimiter{" "};

    for (const auto& [propType, propsValues] : props) {
        out << intentDelimiter << IntentToChar(propType.GetIntent()) << ':' << propType.GetName(graph) << '{';
        TDelimiter valueDelimiter{", "};
        for (auto prop : propsValues.DataNotFinal()) {
            out << valueDelimiter << graph.ToStringByCacheId(prop);
        }
        out << '}';
    }

    return out.Str();
}

struct TUseDiagPropsDebug {
    TUseDiagPropsDebug(const TDepGraph& graph, TDepTreeNode userNode, TPropertyType propType)
        : UserNode(graph, userNode), PropType(propType)
    {
    }
    TNodeDebugOnly UserNode;
    TPropertyType PropType;
};

using TUseDiagPropsDebugOnly = TDebugOnly<TUseDiagPropsDebug>;

inline void UseDiagProps(const TPropsNodeList& props, TFileView moduleName, TDepGraph& graph, TUseDiagPropsDebugOnly debug) {
    for (const auto& id : props) {
        TVector<TStringBuf> args = StringSplitter(GetPropertyValue(graph.GetCmdNameByCacheId(id).GetStr())).Split(' ').SkipEmpty();
        Y_ASSERT(args.size() > 0);
        if (args.size() != 3 || args[0] != "INVALID_TOOL_DIR"sv) {
            // Only INVALID_TOOL_DIR diagnostic is supported currently.
            continue;
        }
        DEBUG_USED(debug);
        BINARY_LOG(IPRP, NProperties::TUseEvent, graph, debug.UserNode.DebugNode, props.DebugNode, debug.PropType, id, "UseDiagProps");
        ui32 dirType = FromString(args[1]);
        ui64 propCacheId = FromString(args[2]);
        TStringBuf dir = graph.GetFileNameByCacheId(static_cast<TDepsCacheId>(propCacheId)).GetTargetStr();
        TScopedContext context(moduleName);
        YConfErr(BadDir) << "Trying to use [[alt1]]TOOL[[rst]] from " << (dirType == EMNT_NonProjDir ? "directory without ya.make: " : "missing directory: ") << "[[imp]]" << dir << "[[rst]]" << Endl;
        TRACE(P, NEvent::TInvalidPeerdir(TString{dir}));
    }
}
inline void TDGIterAddable::SetupPropsPassing(TDGIterAddable* parentIterState, bool mainOutputAsExtra) {
    if (!parentIterState)
        return;

    const TUpdEntryStats& parentState = parentIterState->Entry();
    const TDepTreeNode& parentNode = parentIterState->Node;
    EDepType edge = parentIterState->Dep.DepType;

    parentIterState->SetupReceiveFromChildIntents(parentState, parentNode.NodeType, Node.NodeType, edge, mainOutputAsExtra);

    ResetFetchIntents(parentState.HasChanges, parentState.Props, *parentIterState);
}

inline void TDGIterAddable::UseProps(TYMake& ymake, const TPropertiesState& props, TUsingRules restrictedProps) {
    auto& graph = ymake.Graph;
    auto& symbols = ymake.Graph.Names();

    switch (Node.NodeType) {
        case EMNT_NonParsedFile:
            if (Add && !Add->NeedInit2 && Dep.DepType != EDT_Include) {
                Add->InitDepsRule();
                TAddDepContext context(*Add, ymake.Conf.WriteDepsCache);
                InduceDeps(context, TPropertiesStateWrapper(props, ymake.UpdIter->GetPropsToUse(Node), restrictedProps));

                if (const auto* propValues = props.FindValues(TPropertyType{symbols, EVI_CommandProps, "CfgVars"})) {
                    auto& modData = Add->GetModuleData();
                    if (modData.CmdInfo) {
                        Y_ABORT_UNLESS(!IsFile(modData.CmdInfo->Cmd.EntryPtr->first));
                        bool structCmd = TVersionedCmdId(ElemId(modData.CmdInfo->Cmd.EntryPtr->first)).IsNewFormat();
                        // TODO: handle a case when EntryPtr is null (is it possible at all?)
                        auto dst = structCmd ? Add.Get() : modData.CmdInfo->Cmd.EntryPtr->second.AddCtx;
                        if (!dst) {
                            Y_ABORT_UNLESS(!structCmd);
                            // as of this writing, we may end up here
                            // after entering the shared command node for the second time;
                            // the command node may be shared due to the (legacy) command builder
                            // ignoring the source path and, for example,
                            // making a node like "###:SRCSin=(build_info.cpp.in)"
                            // for both `build_info.cpp.in` and `foobar/build_info.cpp.in`
                            ythrow TError() << "Cannot add CFG_VARS to an unedi(ta)ble node " << modData.CmdInfo->Cmd;
                        }
                        modData.CmdInfo->AddCfgVars(propValues->Data(), Node.ElemId, *dst, structCmd);
                    }
                }
                //if (LeftType == EMNT_BuildCommand && !Add->Module->Incomplete()) {
                // pass props early
                //}
            }
            break;
        case EMNT_Directory:
            if (Add && !Add->NeedInit2) {
                UseByName(*Add, props, TPropertyType{symbols, EVI_GetModules, "DIR_PROPERTIES"}, EDT_Property, EMNT_Property);
                UseByName(*Add, props, TPropertyType{symbols, EVI_GetModules, "MODULES"}, EDT_Include, EMNT_Library);
                YDIAG(V) << "NumDeps: " << Add->Deps.Size() << Endl;
            }

            if (ymake.Conf.ShouldTraverseAllRecurses()) {
                AddIntoQueue(ymake.UpdIter->RecurseQueue, props, TPropertyType{symbols, EVI_GetModules, NProps::TEST_RECURSES}, Node, EDT_Search, EMNT_Directory);
            }

            if (ymake.Conf.ShouldTraverseRecurses()) {
                AddIntoQueue(ymake.UpdIter->RecurseQueue, props, TPropertyType{symbols, EVI_GetModules, NProps::RECURSES}, Node, EDT_Include, EMNT_Directory);
            }

            if (ymake.Conf.ShouldTraverseDepends()) {
                AddIntoQueue(ymake.UpdIter->DependsQueue, props, TPropertyType{symbols, EVI_GetModules, NProps::DEPENDS}, Node, EDT_BuildFrom, EMNT_Directory);
            }

            break;
        case EMNT_Program: // IsModuleType
        case EMNT_Library:
        case EMNT_Bundle: {
            YDIAG(IPRP) << "UseProps " << graph.GetFileName(Node) << Endl;
            TPropertyType confDiagPropType{symbols, EVI_ModuleProps, "CONFIGURE_DIAGNOSTIC"};
            for (const auto& [type, values] : props.GetValues(EVI_ModuleProps)) {
                YDIAG(IPRP) << "UseProps " << graph.GetFileName(Node) << ": intent " << IntentToChar(type.GetIntent()) << Endl;
                if (Add && !Add->NeedInit2) {
                    if (type == confDiagPropType) {
                        UseDiagProps(*values, Add->Module->GetName(), graph, TUseDiagPropsDebugOnly{graph, Node, type});
                    }
                    else {
                        TModuleDirBuilder(*Add->Module, *Add, graph, ymake.Conf.ShouldReportMissingAddincls()).UseModuleProps(type, *values);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

TUpdIter::TUpdIter(TYMake& yMake)
    : TDepthDGIter<TDGIterAddable>(yMake.Graph, TNodeId::Invalid)
    , YMake(yMake)
    , NeverCachePropId(GetNeverCachePropElem(yMake.Graph))
    , Nodes(yMake.Graph)
{
    MainOutputAsExtra = yMake.Conf.MainOutputAsExtra();
}

void TUpdIter::RestorePropsToUse() {
    for (const auto& node : Graph.Nodes()) {
        if (IsOutputType(node->NodeType)) {
            THashSet<TPropertyType> inducedDepsToUse;
            bool isMainOutput = true;
            for (const auto& edge : node.Edges()) {
                if (edge.Value() == EDT_OutTogetherBack) {
                    auto rule = YMake.IncParserManager.IndDepsRuleByPath(Graph.GetFileName(edge.To()));
                    if (rule) {
                        rule->InsertUseActionsTo(inducedDepsToUse);
                    }
                    MainOutputId[edge.To()->ElemId] = node->ElemId;
                } else if (edge.Value() == EDT_OutTogether) {
                    isMainOutput = false;
                    break;
                }
            }

            if (isMainOutput) {
                MainOutputId[node->ElemId] = node->ElemId;

                if (node->NodeType == EMNT_NonParsedFile) {
                    Y_ASSERT(UseFileId(node->NodeType));
                    TFileView name = Graph.GetFileName(node);
                    auto rule = YMake.IncParserManager.IndDepsRuleByPath(name);
                    if (rule) {
                        rule->InsertUseActionsTo(inducedDepsToUse);
                    }
                }

                inducedDepsToUse.insert(TPropertyType{Graph.Names(), EVI_InducedDeps, "*"});

                if (!inducedDepsToUse.empty()) {
                    PropsToUse[node->ElemId] = std::move(inducedDepsToUse);
                }
            }
        }
    }
}

TUsingRules TUpdIter::GetPropsToUse(TDepTreeNode node) const {
    if (!IsOutputType(node.NodeType)) {
        return {};
    }
    const auto mainOutputIdIt = MainOutputId.find(node.ElemId);
    if (mainOutputIdIt == MainOutputId.end()) {
        return {};
    }
    const auto propsToUseIt = PropsToUse.find(mainOutputIdIt->second);
    if (propsToUseIt == PropsToUse.end()) {
        return {};
    }
    return {propsToUseIt->second};
}

NGraphUpdater::ENodeStatus TUpdIter::CheckNodeStatus(const TDepTreeNode& node) const {
    const auto iter = Nodes.find(MakeDepsCacheId(node.NodeType, node.ElemId));
    if (iter == Nodes.end() || iter->second.MarkedAsUnknown) {
        return NGraphUpdater::ENodeStatus::Unknown;
    }
    if (!iter->second.OnceEntered) {
        return NGraphUpdater::ENodeStatus::Waiting;
    }
    if (iter->second.InStack) {
        return NGraphUpdater::ENodeStatus::Processing;
    }
    return NGraphUpdater::ENodeStatus::Ready;
}

inline bool TUpdIter::Enter(TState& state) {
    TDGIterAddable& st = state.back();
    TDGIterAddable* prev = GetStateByDepth(1);
    BINARY_LOG(Iter, NIter::TEnterEvent, Graph, st.Node, prev ? prev->Dep.DepType : EDT_Last, EIterType::MainIter);
    NStats::StackDepthStats.SetMax(NStats::EStackDepthStats::UpdIterMaxStackDepth, state.size());

    if (st.Node.NodeType == EMNT_Deleted)
        return false;

    st.ModulePosition = prev ? prev->ModulePosition : 0;

    // Check if the entered node is induced deps node
    // This info will be checked later in AcceptDep to skip processing
    // of deps for induced deps of the command
    if (prev != nullptr && prev->Dep.DepType == EDT_Property && st.Node.NodeType == EMNT_BuildCommand)
    {
        TStringBuf cmdName = GetCmdName(Graph.GetCmdName(st.Node).GetStr());
        TStringBuf intentName, propName;
        cmdName.Split('.', intentName, propName);
        st.IsInducedDep = IntentByName(intentName, false) == EVI_InducedDeps;
    }

    if (IsOutputType(st.Node.NodeType) || st.Node.NodeType == EMNT_Directory) {
        const auto& data = YMake.Graph.GetFileNodeData(st.Node.ElemId);
        st.MinNodeModTime = data.NodeModStamp;
        if (prev) {
            st.MinNodeModTime = Min(st.MinNodeModTime, prev->MinNodeModTime);
        }
    } else if (prev) {
        st.MinNodeModTime = prev->MinNodeModTime;
    }

    const bool isModule = IsModuleType(st.Node.NodeType);
    if (isModule) {
        Diag()->Where.push_back(st.Node.ElemId, Graph.GetFileName(st.Node).GetTargetStr());
        st.ModulePosition = state.size() - 1;
    }

    const TDepsCacheId id = MakeDepsCacheId(st.Node.NodeType, st.Node.ElemId);
    auto [i, fresh] = Nodes.try_emplace(id, true, TNodeDebugOnly{Graph, id});
    auto statusBeforeEnter = CheckNodeStatus(st.Node);
    fresh = fresh || !i->second.OnceEntered;

    YDIAG(GUpd) << "Enter: " << Graph.ToString(st.Node) << " fresh= " << BoolToChar(fresh) << '\n';

    bool propsPassingSetupDone = false;

    if (fresh) {
        i->second.MarkedAsUnknown = false;

        st.EntryPtr = CurEnt = &*i; // used in functions
        i->second.Props.SetupRequiredIntents(st.Node.NodeType);
        i->second.SetOnceEntered(true);

        if (i->second.AddCtx) {
            i->second.AddCtxOwned = false;
            st.Add = i->second.AddCtx; // TAutoPtr: change ownership
        }

        if (st.Add) {
            YDIAG(Dev) << "Before StartEdit " << Graph.ToString(st.Node) << ": num deps = " << st.Add->Deps.Size() << Endl;
        }

        if (isModule) {
            auto* module = YMake.Modules.Get(st.Node.ElemId);
            Y_ASSERT(module);

            if (module->IsLoaded()) {
                module->ResetIncDirs();
                for (const auto [dirId, ghostType]: module->GhostPeers) {
                    const bool expected = ghostType == EGhostType::Virtual;
                    const auto& path = Graph.Names().FileConf.GetName(dirId).GetTargetStr();
                    const bool actual = !Graph.Names().FileConf.DirHasYaMakeFile(path);
                    if (expected != actual) {
                        st.Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                        return false;
                    }
                }
                if (st.ModuleDirsChanged(YMake, *module)) {
                    YDebug() << "Module " << module->GetName() << " is reconfigured due to SRCDIR or ADDINCL changes" << Endl;
                    st.Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                    return false;
                }
            }
        } else if (st.Node.NodeType == EMNT_NonParsedFile && statusBeforeEnter != NGraphUpdater::ENodeStatus::Ready) {
            if (!Graph.GetFileName(st.Node).IsLink()) {
                if (const auto* module = YMake.Modules.Get(state[st.ModulePosition].Node.ElemId); module && module->IsLoaded()) {
                    if (!module->GetOwnEntries().has(st.Node.ElemId) && !module->GetSharedEntries().has(st.Node.ElemId)) {
                        state[st.ModulePosition].Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                        i->second.MarkedAsUnknown = true;
                        return false;
                    }
                }
            }
        }

        if (st.Node.NodeType == EMNT_Directory) {
            FORCE_UNIQ_CONFIGURE_TRACE(Graph.GetFileName(st.Node.ElemId), H, NEvent::TNeedDirHint(TString(Graph.GetFileName(st.Node.ElemId).CutType())));
        }

        EMakeNodeType oldNodeType = st.Node.NodeType;
        st.StartEdit(YMake, *this);

        if (st.Node.NodeType != oldNodeType) {
            // πάντα ρεῖ...
            i->second.Props.SetupRequiredIntents(st.Node.NodeType);
        }

        // StartEdit may insert to Nodes new elements.
        // Depending on Nodes type it may also invalidate existing iterators. Then uncomment the following:
        // i = Nodes.find(id);
        // StartEdit may change NodeType and ElemId. Try to merge our node with existing one
        const TDepsCacheId new_id = MakeDepsCacheId(st.Node.NodeType, st.Node.ElemId);
        if (new_id != id) {
            auto [ii, added] = Nodes.try_emplace(new_id, i->second);
            fresh = added || !ii->second.OnceEntered;
            if (added) {
                // The key of node was changed and there hasn't been node with such a new key in Nodes until now: so we've just added our node under this new key
                YDIAG(VV) << "Node is added for updated id " << ElemId(id) << " -> " << ElemId(new_id) << " for " << Graph.ToString(st.Node) << Endl;
                if (st.Add) {
                    i->second.AddCtx = nullptr;
                    st.Add->Entry = ii->second;
                }
                i = ii;
                st.EntryPtr = CurEnt = &*i;
            }
            if (!fresh) {
                st.Add = ii->second.AddCtx;
                YDIAG(V) << "Node was merged with existing for " << Graph.ToString(st.Node) << Endl;
            } else if (st.Add && st.Add->UpdNode != TNodeId::Invalid) {
                st.Add->NeedInit2 = true;
                st.Add->Deps.Clear();
                YDIAG(Dev) << "Run StartEdit again for " << Graph.ToString(st.Node) << Endl;
                st.StartEdit(YMake, *this);
            }
        }

        // Правильно было бы делать этот вызов в самом начале Enter и повторно при изменении типа узла после StartEdit.
        // Но сейчас поведение StartEdit зависит от устанавливаемых этим вызовом значений st.FetchIntents.
        // При этом из-за текущего порядка вызовов StartEdit всегда получает пустой набор FetchIntents,
        // а если бы получал правильные значения, то это приводило бы к большому количеству ненужных повторных
        // разборов makefile-ов. Нужно сначала эту ситуацию исследовать, убрать или корректно изменить
        // зависимость StartEdit от FetchIntents и после этого перенести вызов SetupPropsPassing в более правильное место.
        st.SetupPropsPassing(prev, MainOutputAsExtra);
        propsPassingSetupDone = true;

        if (!isModule && IsModuleType(st.Node.NodeType)) {
            // StartEdit have changed NodeType
            Diag()->Where.push_back(st.Node.ElemId, Graph.GetFileName(st.Node).GetTargetStr());
            st.ModulePosition = state.size() - 1;
        }

        if (st.Add) {
            YDIAG(Dev) << "After StartEdit " << Graph.ToString(st.Node) << ": num deps = " << st.Add->Deps.Size() << Endl;
        }

        if (fresh) {
            if (st.Node.NodeType == EMNT_File) {
                const auto& nodeData = YMake.Graph.GetFileNodeData(st.Node.ElemId);
                CurEnt->second.PassInducedIncludesThroughFiles = nodeData.PassInducedIncludesThroughFiles;
            } else if (st.Node.NodeType == EMNT_NonParsedFile) {
                const auto& nodeData = YMake.Graph.GetFileNodeData(st.Node.ElemId);
                CurEnt->second.PassInducedIncludesThroughFiles = nodeData.PassInducedIncludesThroughFiles;
                CurEnt->second.PassNoInducedDeps = nodeData.PassNoInducedDeps;
            }

            if (EqualToOneOf(st.Node.NodeType, EMNT_File, EMNT_MakeFile, EMNT_MissingFile)) {
                const auto& fileData = YMake.Names.FileConf.GetFileDataById(st.Node.ElemId);
                CurEnt->second.IncModStamp = fileData.RealModStamp;
                CurEnt->second.ModStamp = fileData.RealModStamp;
            }

            if ((st.Node.NodeType == EMNT_BuildCommand || st.Node.NodeType == EMNT_Property) && IsPropDep(state)) {
                if (st.NodeToProps(Graph, DelayedSearchDirDeps, st.Add.Get())) {
                    ui8 timestamp = 0;
                    if (UseFileId(prev->Node.NodeType)) {
                        timestamp = prev->Entry().IncModStamp;
                    }
                    st.Entry().Props.SetIntentsReady(TIntents::All(), timestamp, TPropertiesState::ENotReadyLocation::NodeToProps);
                }
                if (!st.IsEdited() && st.Node.NodeType == EMNT_BuildCommand && state.size() > 2 && IsModuleType(prev->Node.NodeType)) {
                    const auto prop = Graph.GetCmdName(st.Node.NodeType, st.Node.ElemId).GetStr();
                    ui64 modId;
                    TStringBuf name, val;
                    ParseCommandLikeProperty(prop, modId, name, val);
                    // LATE_GLOB is empty for invocations without any pattern like `_LATE_GLOB(VarName)`
                    // the only case when the node deps can change is ya.make modification which adds some
                    // patterns to the macro invocation. Thus we can skip trying to update glob node here.
                    if (name == NProps::LATE_GLOB && !val.empty()) {
                        auto* module = YMake.Modules.Get(prev->Node.ElemId);
                        Y_ASSERT(module);
                        UpdateGlobNode(*this, st, val, Graph.GetFileName(module->GetDirId()));
                    }
                }
            }
        }
    }

    if (!propsPassingSetupDone) {
        st.SetupPropsPassing(prev, MainOutputAsExtra);
        propsPassingSetupDone = true;
    }

    if (prev) {
        prev->Entry().IsMultiModuleDir |= IsAtDir2MultiModulePropertyDep(state, Graph);
    }

    st.EntryPtr = CurEnt = &*i;

    if (Diag()->Iter) {
        ui64 curElemId = st.Node.ElemId;
        EMakeNodeType curType = st.Node.NodeType;

        ui64 prevElemId = 0;
        EMakeNodeType prevType = EMNT_UnknownCommand;
        TString prevName = "unknown";
        EDepType depType = EDT_Last;
        if (prev) {
            prevElemId = prev->Node.ElemId;
            prevName = Graph.ToString(prev->Node);
            prevType = prev->Node.NodeType;
            depType = prev->Dep.DepType;
        }
        YDIAG(Iter) << "Enter: " << prevElemId << " " << prevType << " " << prevName
                     << " -" << depType << "> "
                     << curElemId << " " << curType << " " << Graph.ToString(st.Node) << Endl;
        YDIAG(Iter) << "Fresh? " << fresh << Endl;
    }

    st.WasFresh = fresh;

    if (isModule) {
        auto* module = YMake.Modules.Get(st.Node.ElemId);
        Y_ASSERT(module);

        if (YMake.Conf.TransitionSource != ETransition::None && module->Transition != ETransition::None && module->Transition != YMake.Conf.TransitionSource) {
            if (YMake.Conf.ReportPicNoPic) {
                if (module->Transition == ETransition::Pic) {
                    FORCE_UNIQ_CONFIGURE_TRACE(module->GetDir(), T, PossibleForeignPlatformEvent(module->GetDir(), NEvent::TForeignPlatformTarget::PIC));
                } else if (module->Transition == ETransition::NoPic) {
                    FORCE_UNIQ_CONFIGURE_TRACE(module->GetDir(), T, PossibleForeignPlatformEvent(module->GetDir(), NEvent::TForeignPlatformTarget::NOPIC));
                }
            }
            if (!YMake.Conf.DescendIntoForeignPlatform) {
                return false;
            }
        }
    }

    return fresh;
}

inline void TUpdIter::Leave(TState& state) {
    CurEnt->second.InStack = false;
    TDGIterAddable& st = state.back();
    YDIAG(Iter) << "Leave: " << st.Node.ElemId << " " << st.Node.NodeType
                 << " " << Graph.ToString(st.Node) << Endl;
    BINARY_LOG(Iter, NIter::TLeaveEvent, Graph, st.Node, EIterType::MainIter);

    st.Flush(state);
    LastNode = st.NodeStart;
    LastType = st.Node.NodeType;
    LastElem = st.Node.ElemId;
    // NOTE: EMNT_MakeFile is treated as a special sort of Property
    // TODO: figure out more generic approach, also connect Directory to Makefile via EDT_Property (not EDT_Include)
    if (EqualToOneOf(st.Node.NodeType, EMNT_File, EMNT_MakeFile, EMNT_MissingFile)) {
        CurEnt->second.IncModStamp = Max(CurEnt->second.IncModStamp, CurEnt->second.ModStamp);
        if (LastType == EMNT_MakeFile) {
            CurEnt->second.Props.UpdateIntentTimestamp(EVI_GetModules, CurEnt->second.IncModStamp);
        }
    }
    if (DirListPropDep(state)) {
        YMake.Names.FileConf.ListDir(st.Node.ElemId);
        const TFileData &fileData = YMake.Names.FileConf.GetFileDataById(st.Node.ElemId);
        auto &mst = state[state.size() - 3].Entry().IncModStamp; // EMNT_MakeFile by design of DirListPropDep()
        mst = Max(mst, fileData.RealModStamp);
    }

    // Properties with recurses and globs have to be applied at every run, even if no changes were detected
    if (state.size() >= 3) {
        auto& prev = state[state.size() - 2];
        auto& pprev = state[state.size() - 3];
        auto isMakefileProperty = IsMakeFilePropertyDep(prev.Node.NodeType, prev.Dep.DepType, st.Node.NodeType) && pprev.IsAtDirMkfDep(prev.Node.NodeType);
        auto isModuleProperty = IsModulePropertyDep(prev.Node.NodeType, prev.Dep.DepType, st.Node.NodeType) && st.Node.NodeType == EMNT_BuildCommand;
        auto isMakefileIncludeFile = IsMakeFileIncludeDep(prev.Node.NodeType, prev.Dep.DepType, st.Node.NodeType) && pprev.IsAtDirMkfDep(prev.Node.NodeType);

        if (isMakefileProperty || isModuleProperty) {
            ui64 propId;
            TStringBuf propName, propValue;
            ParseCommandLikeProperty(Graph.GetCmdName(st.Node).GetStr(), propId, propName, propValue);

            if (!prev.Entry().HasChanges) {
                if (EqualToOneOf(propName, NProps::RECURSES, NProps::TEST_RECURSES, NProps::DEPENDS)) {
                    TPropertyType propType{Graph.Names(), EVI_GetModules, propName};
                    TDeps deps;
                    ReadDeps(Graph, st.NodeStart, deps);
                    TPropertySourceDebugOnly sourceDebug{EPropertyAdditionType::Created};
                    prev.Entry().DepsToInducedProps(propType, deps, sourceDebug);

                } else if (propName == NProps::GLOB) {
                    if (GlobNeedUpdate(st, propValue, Graph.GetFileName(pprev.Node))) {
                        prev.Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
                        st.Entry().SetOnceEntered(false);
                        st.ForceReassemble();
                    }
                }
            }
        } else if (
            st.Node.ElemId == NeverCachePropId &&
            !prev.Entry().HasChanges &&
            (IsMakeFileType(prev.Node.NodeType) || IsModuleType(prev.Node.NodeType)) && IsPropertyDep(prev.Node.NodeType, prev.Dep.DepType, st.Node.NodeType)
        ) {
            prev.Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
            st.Entry().SetOnceEntered(false);
            st.ForceReassemble();
        }
        else if (isMakefileIncludeFile && !prev.Entry().HasChanges && CurEnt->second.HasChanges) {
            prev.Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
            st.Entry().SetOnceEntered(false);
            st.ForceReassemble();
        }
    }

    if (IsModuleType(st.Node.NodeType) && st.WasFresh) {
        Instance()->IncConfModulesDone();
    }

    if (Diag()->Where.size() && IsModuleType(st.Node.NodeType)) {
        Y_ASSERT(Diag()->Where.back().first == st.Node.ElemId &&
                 Diag()->Where.back().second == Graph.GetFileName(st.Node).GetTargetStr());
        Diag()->Where.pop_back();
    }

    // After leaving a module - delete it's builder
    if (st.Add && st.Add->IsModule) {
        delete st.Add->ModuleBldr;
        st.Add->ModuleBldr = nullptr;
        st.Add->Module->NotifyBuildComplete();
        ResolveCaches.Drop(st.Add->Module->GetId());
        YDIAG(GUpd) << "Deleted builder for module " << st.Add->ElemId << "\n";
    }
}

bool TUpdIter::DirectDepsNeedUpdate(const TDGIterAddable& st, const TDepTreeNode& oldDepNode) {
    const auto isPeerdir = IsPeerdirDep(st.Node.NodeType, st.Dep.DepType, st.Dep.DepNode.NodeType);
    const auto isTooldir = IsTooldirDep(st.Node.NodeType, st.Dep.DepType, st.Dep.DepNode.NodeType)
        || !Deleted(oldDepNode) && st.Dep.DepNode.NodeType != oldDepNode.NodeType && IsTooldirDep(st.Node.NodeType, st.Dep.DepType, oldDepNode.NodeType);
    if (!isPeerdir && !isTooldir) {
        return false;
    }

    if (isTooldir && st.Add) {
        return false;
    }

    const TModule* module = nullptr;

    if (isPeerdir) {
        if (st.Add && st.Add->IsModule) {
            module = st.Add->Module;
        } else if (!st.Add && IsModuleType(st.Node.NodeType)) {
            module = YMake.Modules.Get(st.Node.ElemId);
        }
        if (!module || !module->IsLoaded()) {
            return false;
        }
    }

    const auto dirNode = Graph.GetNodeById(LastType, LastElem);
    if (dirNode.IsValid()) {
        const auto node = Graph.GetNodeById(st.Node);
        auto request = isPeerdir ? TMatchPeerRequest::CheckAll() : TMatchPeerRequest{false, true, {}};
        const auto peerNode = NPeers::GetPeerNode(YMake.Modules, dirNode, module, std::move(request));
        const auto directPeerNode = NPeers::GetDirectPeerNode(Graph, YMake.Modules, node, dirNode.Id(), isTooldir);

        if ((peerNode.Status == EPeerSearchStatus::Match) != directPeerNode.IsValid()) {
            return true;
        }

        if (peerNode.Status == EPeerSearchStatus::Match && directPeerNode.Id() != peerNode.Node.Id()) {
            return true;
        }
    }
    return false;
}

TGetPeerNodeResult TUpdIter::GetPeerNodeIfNeeded(const TDGIterAddable& st){
    const auto node = st.Add.Get();
    if (node != nullptr && node->IsModule && IsPeerdirDep(st.Node.NodeType, st.Dep.DepType, st.Dep.DepNode.NodeType)) {
        const auto dirNode = Graph.GetNodeById(LastType, LastElem);
        if (!dirNode.IsValid() || !IsDirType(dirNode->NodeType)) {
            return {Graph.Get(TNodeId::Invalid), EPeerSearchStatus::Error};
        }

        TStringBuf dir = Graph.GetFileName(dirNode).GetTargetStr();
        auto isUserSpecifiedPeerdir = st.Add->ModuleDef && st.Add->ModuleDef->IsMakelistPeer(dir);
        isUserSpecifiedPeerdir |= st.Add->GetModuleData().IsParsedPeer(dirNode->ElemId);

        const auto nodeModule = node->Module;

        if (nodeModule != nullptr) {
            auto checkAllTags = isUserSpecifiedPeerdir && !nodeModule->GetTag().empty();
            auto request = isUserSpecifiedPeerdir ? (checkAllTags? TMatchPeerRequest::CheckAll() : TMatchPeerRequest{true, false, {EPeerSearchStatus::DeprecatedByTags}}) : TMatchPeerRequest{false, false, {EPeerSearchStatus::DeprecatedByFilter}};
            auto peerNode = NPeers::GetPeerNode(YMake.Modules, dirNode, nodeModule, std::move(request));

            if (!isUserSpecifiedPeerdir && peerNode.Status != EPeerSearchStatus::Match) {
                peerNode.Status = EPeerSearchStatus::Match;
                YDIAG(IPRP) << "Ignore bad PEERDIR to " << dir << " because it is not user-specified" << Endl;
            }
            return peerNode;
        }

        if (!isUserSpecifiedPeerdir) {
            // FIXME: stop to silently ignore this case
            YDIAG(IPRP) << "Ignore PEERDIR from bad module to " << dir << " because it is not user-specified" << Endl;
            return {Graph.Get(TNodeId::Invalid), EPeerSearchStatus::Match};
        }

        return {Graph.Get(TNodeId::Invalid), EPeerSearchStatus::Unknown};
    }

    return {Graph.Get(TNodeId::Invalid), EPeerSearchStatus::Match};
}

void TUpdIter::PropagateIncDirs(const TDGIterAddable& st) const {
    TModule* childModule = YMake.Modules.Get(LastElem);
    if (!childModule) {
        return;
    }

    if (!childModule->IsDirsComplete()) {
        TModuleRestorer restorer({YMake.Conf, Graph, YMake.Modules}, Graph.GetNodeById(LastType, LastElem));
        childModule = restorer.RestoreModule();
    }

    TModule* module = nullptr;
    if (st.Add) {
        Y_ASSERT(st.Add->IsModule);
        module = st.Add->Module;
    } else {
        module = YMake.Modules.Get(st.Node.ElemId);
    }

    if (module) {
        Y_ASSERT(childModule->IsDirsComplete());
        childModule->IncDirs.PropagateTo(module->IncDirs);
    }
}

inline void TUpdIter::Left(TState& state) {
    YDIAG(Iter) << "Left: " << ElemId(CurEnt->first) << " : " << Endl;
    TEntryPtr childEnt = CurEnt;
    TDGIterAddable& st_ = state.back(); // Rescan may invalidate st_
    NDebugEvents::NIter::TLeftEventLogEventGuard leftEventGuard{
        &Graph, CurEnt->first, st_.EntryPtr->first, EIterType::MainIter
    };
    st_.Dep.DepNodeId = LastNode;
    TDepTreeNode oldDepNode{};
    if (auto node = Graph.Get(LastNode); node.IsValid()) {
        oldDepNode = std::exchange(st_.Dep.DepNode, node.Value());
    }
    if (st_.Add && !st_.Add->NeedInit2 && st_.Dep.DepNode.ElemId != LastElem) {
        // Child node was changed, update NodeType and ElemId in Deps
        st_.Add->Deps.Replace(st_.CurDep - 1, st_.Add->Deps[st_.CurDep - 1].DepType, LastType, LastElem);
    }
    CurEnt = st_.EntryPtr;

    TUpdEntryStats& currStats = CurEnt->second;
    TUpdEntryStats& chldStats = childEnt->second;

    if (st_.Node.NodeType == EMNT_File || st_.Node.NodeType == EMNT_MakeFile) {
        currStats.IncModStamp = Max(currStats.IncModStamp, chldStats.IncModStamp);
    }

    TPropertiesState& currProps = currStats.Props;
    TPropertiesState& chldProps = chldStats.Props;

    // TODO/FIXME: this may report "<invalid node>" when dealing with not-yet-flushed nodes (use extra outputs in module commands to reproduce)
    YDIAG(IPRP) << "Left from " << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << " to " << Graph.ToString(st_.Node) << ": "
                << "CurrProps [" << currProps.DumpValues(Graph) << "] "
                << "FromProps [" << chldProps.DumpValues(Graph) << "] "
                << "Pass " << st_.DumpReceiveIntents() << " Use " << currProps.DumpRequiredIntents() << " Not ready " << chldProps.DumpNotReadyIntents() << '\n';

    if (st_.HasIntentsToReceiveFromChild()) {
        FOR_ALL_INTENTS(intent) {
            if (st_.ShouldReceiveIntentFromChild(intent)) {
                YDIAG(IPRP)
                    << "      stamp[" << IntentToChar(intent) << "]: "
                    << chldProps.DumpIntentTimestamp(intent) << " -> "
                    << currProps.DumpIntentTimestamp(intent) << '\n';

                currProps.UpdateIntentTimestamp(intent, chldProps);
            }
        }
    }

    bool needEdit = false;

    bool isFile = UseFileId(st_.Node.NodeType);
    Y_ASSERT(isFile || !currProps.HasRequiredIntents());

    if (isFile && currProps.HasRequiredIntents()) {
        ui8 nodeModStamp = Graph.GetFileNodeData(st_.Node.ElemId).NodeModStamp;
        FOR_ALL_INTENTS(intent) {
            if (currProps.IsIntentRequired(intent) && chldProps.IsIntentNewerThan(intent, nodeModStamp)) {

                YDIAG(IPRP)
                    << "      +needEdit [" << IntentToChar(intent) << "] "
                    << (int)nodeModStamp << " < " << chldProps.DumpIntentTimestamp(intent) << '\n';

                needEdit = true;
                break;
            }
        }
    }

    // Неготовые свойства дочернего узла, которые текущий узел должен использовать или передать родительским узлам.
    TIntents missingProps = chldProps.GetNotReadyIntents() & (st_.IntentsToReceiveFromChild() | currProps.GetRequiredIntents());

    // Ставим флаг неготовности всем runtime-свойствам текущего узла, которые мы использовали или передавали бы.
    // Графовым свойствам такой флаг ставить не нужно, их всегда можно запросить через Rescan.
    currProps.SetIntentsNotReady(missingProps & GetRuntimeIntents(), TPropertiesState::ENotReadyLocation::MissingFromChild);

    if ((needEdit || st_.IsEdited()) && missingProps.NonEmpty()) {
        bool makefileRescan = missingProps.Has(EVI_GetModules) && st_.IsAtDirMkfDep(LastType);
        bool freshRescan = !chldStats.AddCtx && !st_.EntryPtr->second.HasChanges;

        if (makefileRescan && freshRescan) {
            YDIAG(IPRP) << "Have to rescan " << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << ", force reassemble" << Endl;
            chldStats.SetOnceEntered(false);
            st_.ForceReassemble();
            return;
        }

        // Делаем Rescan только для тех свойств, которые понадобятся в текущем узле.
        // Если родительским узлам понадобятся другие свойства, они сами сделают Rescan.
        missingProps = chldProps.GetNotReadyIntents() & currProps.GetRequiredIntents() & GetNonRuntimeIntents();
        if (MainOutputAsExtra) {
            missingProps = missingProps & st_.IntentsToReceiveFromChild();
        }

        if (!missingProps.Empty() || !MainOutputAsExtra) {
            YDIAG(IPRP) << "      Rescan " << Endl;
            Rescan(st_);
            // TODO/FIXME: this may report "<invalid node>" when dealing with not-yet-flushed nodes (use extra outputs in module commands to reproduce)
            YDIAG(IPRP) << "After Rescan from " << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << ": "
                        << "FromProps[ " << chldProps.DumpValues(Graph) << " ] "
                        << " Not ready " << chldProps.DumpNotReadyIntents() << Endl;

            Y_ASSERT((chldProps.GetNotReadyIntents() & currProps.GetRequiredIntents() & GetNonRuntimeIntents()).Empty());
        }
    }
    TDGIterAddable& st = state.back();

    if (!needEdit && chldStats.HasChanges && DirectDepsNeedUpdate(st, oldDepNode) ) {
        if (IsPeerdirDep(st.Node.NodeType, st.Dep.DepType, st.Dep.DepNode.NodeType)) {
            YDIAG(IPRP) << "Peerdirs status have been changed, set needEdit" << Endl;
            needEdit = true;
        } else {
            YDIAG(IPRP) << "Tooldirs status have been changed, mark ModuleProps as not ready" << Endl;
            currProps.SetIntentNotReady(EVI_ModuleProps, YMake.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
        }
    } else if (!needEdit && IsTooldirDep(st.Node.NodeType, st.Dep.DepType, st.Dep.DepNode.NodeType)) {
        if (!Graph.Names().CommandConf.GetById(TVersionedCmdId(st.Node.ElemId).CmdId()).KeepTargetPlatform) {
            const auto dirNode = Graph.GetNodeById(LastType, LastElem);
            const auto toolDir = Graph.GetFileName(dirNode);
            FORCE_UNIQ_CONFIGURE_TRACE(toolDir, T, PossibleForeignPlatformEvent(toolDir, NEvent::TForeignPlatformTarget::TOOL));
        }
    }

    if (needEdit && !st.IsEdited()) {
        // TODO: use all properties in StartEdit?
        YDIAG(IPRP) << "      Edit " << Graph.ToString(st.Node) << Endl;
        if (st.Add && st.Add->IsModule && !st.Add->ModuleDef || !st.Add && (IsModuleType(st.Node.NodeType) || IsModuleType(LastType))) {
            // NukeModuleDir will emit subsequent events. Should preserve correct events order.
            leftEventGuard.Log();

            NukeModuleDir(state);
            return;
        }
        auto curDep = st.CurDep;
        st.StartEdit(YMake, *this);
        if ((st.Dep.DepType != EDT_Include && st.Node.NodeType == EMNT_NonParsedFile) || st.Node.NodeType == EMNT_Directory) {
            // must be re-filled from properties
            st.Add->RemoveIncludeDeps(curDep);
        }
        // can't be EMNT_File so we don't need to check ModTime here
    }
    Y_ASSERT(!currStats.IsMultiModuleDir || !currStats.Props.HasValues());
    // TODO/FIXME: this may report "<invalid node>" when dealing with not-yet-flushed nodes (use extra outputs in module commands to reproduce)
    YDIAG(GUpd) << "Left from " << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << " to " << Graph.ToString(st.Node) << Endl;
    // we process induces deps only while building or rebuilding the node

    const auto peerNode = GetPeerNodeIfNeeded(st);
    if (peerNode.Status == EPeerSearchStatus::Error) {
        YDIAG(IPRP) << "Bad invocation of GetPeerEntry, propagation not done" << Endl;
        return;
    }

    if (childEnt != nullptr && st.Dep.DepType != EDT_OutTogetherBack) {
        const auto& childProps = chldStats.Props;
        if (childProps.HasValues()) {
            auto restrictedProps = chldStats.GetRestrictedProps(st.Dep.DepType, GetPropsToUse({LastType, LastElem}));

            // Теоретически, здесь мы должны использовать графовые свойста дочернего узла,
            // только если текущий узел уже находится в состоянии редактирования.
            // Но на данный момент UseProps сам проверяет это условие.
            st.UseProps(YMake, childProps, restrictedProps);

            // Передаём все готовые runtime-свойства из дочернего узла.
            // Мы должны передать их все при первом же выходе, потому что TUpdIter
            // не будет повторно спускаться в дочерние узлы. И если в текущий узел
            // мы придём по другой дуге, по которой нужно будет передавать
            // другие runtime свойства, у нас уже не будет возможности их получить.
            TIntents receiveAllRuntimeIntentsFromChild = GetRuntimeIntents() & st.IntentsToReceiveFromChild();

            // Раньше здесь был перенос только готовых свойств.
            //
            // copyIntents = receiveAllRuntimeIntentsFromChild.Without(chldProps.GetNotReadyIntents());
            //
            // Однако в старой реализации хоть флаг not ready и передавался отдельно от самих свойств,
            // сами значения свойств тоже распространялись отдельным вызовом Rescan.
            // Сейчас Rescan для runtime свойств не производится, поэтому сразу распространяем
            // их значения даже вместе с флагом not ready.
            currStats.Props.CopyProps(chldStats.Props, receiveAllRuntimeIntentsFromChild, restrictedProps);
            YDIAG(IPRP) << "UpdIter: after adding: "
                        << "Props[ " << currProps.DumpValues(Graph) << " ] " << Endl;
        } else {
            YDIAG(IPRP) << "No suitable child props, propagation not done" << Endl;
        }

        if (IsDirectPeerdirDep(st.Node.NodeType, st.Dep.DepType, st.Dep.DepNode.NodeType)) {
            PropagateIncDirs(st);
        }
    }

    if (TNodeAddCtx* node = st.Add.Get()) {
        TAddDepIter& dep = st.Dep;
        if (node->IsModule && !node->NeedInit2) {
            if (st.AtEnd()) {
                if (!node->Module->IsInputsComplete()) {
                    AssertEx(node->ModuleBldr != nullptr, "Module was not processed");
                    node->ModuleBldr->RecursiveAddInputs();
                }
            }

            if (IsPeerdirDep(st.Node.NodeType, dep.DepType, dep.DepNode.NodeType)) {
                if (IsInvalidDir(LastType)) {
                    YConfErr(BadDir) << "[[alt1]]PEERDIR[[rst]] to " << (LastType == EMNT_NonProjDir ? "directory without ya.make: " : "missing directory: ") << "[[imp]]"
                                     << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << "[[rst]]" << Endl;
                    TRACE(P, NEvent::TInvalidPeerdir(TString{Graph.GetFileName(LastType, LastElem).GetTargetStr()}));
                    return;
                }
                switch (peerNode.Status) {
                    case EPeerSearchStatus::Match:
                        if (peerNode.Node.IsValid()) {
                            // Add direct peerdir
                            node->AddUniqueDep(st.Dep.DepType, peerNode.Node.Value().NodeType, peerNode.Node.Value().ElemId);
                        }
                        break;
                    case EPeerSearchStatus::DeprecatedByTags:
                        YConfErr(BadDir) << "[[alt1]]PEERDIR[[rst]] from module tagged [[alt1]]" << node->Module->GetTag()
                                         << "[[rst]] to [[imp]]" << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << "[[rst]] is prohibited: tags are incompatible" << Endl;
                        break;
                    case EPeerSearchStatus::DeprecatedByRules:
                        YConfErr(BadDir) << "[[alt1]]PEERDIR[[rst]] from [[imp]]" << node->Module->GetDir() << "[[rst]] to [[imp]]"
                                         << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << "[[rst]] is prohibited by peerdir policy." << Endl
                                         << "See https://docs.yandex-team.ru/ya-make/manual/common/peerdir_rules#policy for details" << Endl;
                        break;
                    case EPeerSearchStatus::DeprecatedByInternal:
                        YConfErr(BadDir) << "[[alt1]]PEERDIR[[rst]] from [[imp]]" << node->Module->GetDir() << "[[rst]] to [[imp]]"
                                         << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << "[[rst]] is prohibited since latter is `internal` and former is not its sibling." << Endl
                                         << "See https://docs.yandex-team.ru/ya-make/manual/common/peerdir_rules#internal for details" << Endl;
                        break;
                    case EPeerSearchStatus::DeprecatedByFilter:
                        YConfErr(BadDir) << "[[alt1]]PEERDIR[[rst]] to [[imp]]" << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << "[[rst]] is not allowed: module types are incompatible" << Endl;
                        break;
                    case EPeerSearchStatus::NoModules:
                        YConfErr(BadDir) << "No modules for [[alt1]]PEERDIR[[rst]] in [[imp]]" << Graph.ToString(Graph.GetNodeById(LastType, LastElem)) << "[[rst]] " << Endl;
                        break;
                    default:
                        break;
                }
            }
        } else if (st.Node.NodeType == EMNT_BuildCommand && dep.DepType == EDT_Include && (state.size() < 2 || state[state.size() - 2].Dep.DepType != EDT_Property)) { // TOOL dep or InnerCommandDep
            if (IsInvalidDir(LastType)) {
                TStringBuilder msg;
                msg << "INVALID_TOOL_DIR"sv << ' ' << static_cast<ui32>(LastType) << ' ' << LastElem;
                ui64 propElemId = Graph.Names().AddName(EMNT_Property, FormatProperty("Mod.CONFIGURE_DIAGNOSTIC", msg));
                node->AddUniqueDep(EDT_Property, EMNT_Property, propElemId);
            } else if (LastType == EMNT_Directory) {
                const auto dirNode = Graph.GetNodeById(LastType, LastElem);
                const auto libNode = NPeers::GetPeerNode(YMake.Modules, dirNode, nullptr, TMatchPeerRequest{false, true, {}});
                if (libNode.Status == EPeerSearchStatus::Match) {
                    // Direct tooldir
                    node->AddUniqueDep(st.Dep.DepType, libNode.Node.Value().NodeType, libNode.Node.Value().ElemId);
                    if (!Graph.Names().CommandConf.GetById(TVersionedCmdId(st.Node.ElemId).CmdId()).KeepTargetPlatform) {
                        const auto toolDir = Graph.GetFileName(dirNode);
                        FORCE_UNIQ_CONFIGURE_TRACE(toolDir, T, PossibleForeignPlatformEvent(toolDir, NEvent::TForeignPlatformTarget::TOOL));
                    }
                }
                else if (libNode.Status == EPeerSearchStatus::NoModules) {
                    YConfErr(BadDir) << "[[alt1]]TOOL[[rst]] not found: no modules in " << Graph.GetFileName(LastType, LastElem) << Endl;
                } else if (libNode.Status == EPeerSearchStatus::DeprecatedByFilter) {
                    YConfErr(BadDir) << "No suitable [[alt1]]TOOL[[rst]] module found in " << Graph.GetFileName(LastType, LastElem) << Endl;
                }
            } else if (LastType == EMNT_BuildCommand && Graph.Names().CommandConf.GetById(LastElem).KeepTargetPlatform) { // IsInnerCommandDep
                Graph.Names().CommandConf.GetById(node->ElemId).KeepTargetPlatform = true;
            }
        }
    }

    // Add node we are currently leaving to parent module's ALL_SRCS, if it matches the condition:
    auto& dep = st.Dep;
    bool isSourceFile = dep.DepType == EDT_BuildFrom && dep.DepNode.NodeType == EMNT_File;
    bool isGlobOrGroup = dep.DepType == EDT_BuildFrom && dep.DepNode.NodeType == EMNT_BuildCommand;
    bool isHeader = dep.DepType == EDT_Search && dep.DepNode.NodeType == EMNT_File;
    if (isSourceFile || isGlobOrGroup || isHeader) {
        auto* moduleBuilder = st.GetParentModuleBuilder(*this);
        if (moduleBuilder && moduleBuilder->ShouldAddAllSrcs()) {
            moduleBuilder->AllSrcs.AddDep(dep.DepNode);
        }
    }
}

void TUpdIter::NukeModuleDir(TState& state) {
    TStringBuf modName = state.back().Add ? state.back().Add->Module->GetFileName() : Graph.GetFileName(state.back().Node).GetTargetStr();
    while (!state.empty()) { // there may be Dir -> Test Module -> Main module case
        if (!IsModuleType(state.back().Node.NodeType))
            break;
        TDGIterAddable& st = state.back();
        Y_ASSERT(!st.Add || st.Add->NeedInit2); // we don't need to call Leave() or Flush() here
        if (st.Add && st.Add->IsModule) {
            st.Add->NukeModule();
        }
        st.Entry().Props.ClearValues();
        st.Entry().SetOnceEntered(false);
        Y_ASSERT(Diag()->Where.back().first == st.Node.ElemId &&
                 Diag()->Where.back().second == Graph.GetFileName(st.Node).GetTargetStr());
        Diag()->Where.pop_back();
        BINARY_LOG(Iter, NIter::TRawPopEvent, Graph, st.Node);
        state.pop_back();
    }
    if (state.empty() || !IsDirType(state.back().Node.NodeType)) { // WTF?
        ythrow TNotImplemented() << "NukeModuleDir: directory for " << modName << " not found, check PEERDIR's";
    }
    TDGIterAddable& st = state.back();
    YDIAG(IPRP) << "Will nuke directory " << Graph.ToString(st.Node) << " due to incomplete module " << modName << Endl;
    st.CurDep = 0; // rewind to get 1st dep
    st.InitDeps();
    while (!st.AtEnd()) {
        auto nodeIt = Nodes.find(MakeDepsCacheId(st.Dep.DepNode.NodeType, st.Dep.DepNode.ElemId));
        if (nodeIt != Nodes.end()) {
            if (IsModuleType(st.Dep.DepNode.NodeType)) {
                if (auto* module = YMake.Modules.Get(st.Dep.DepNode.ElemId); module) {
                    for (auto id : module->GetOwnEntries()) {
                        if (auto i = Nodes.find(MakeDepFileCacheId(id)); i != Nodes.end()) {
                            i->second.MarkedAsUnknown = true;
                        }
                    }
                }
            }
            nodeIt->second.Props.ClearValues();
            nodeIt->second.SetOnceEntered(false);
        }
        st.NextDep();
    }
    st.ForceReassemble();
    Stats.Inc(NStats::EUpdIterStats::NukedDir);
}

inline TUpdIter::EDepVerdict TUpdIter::AcceptDep(TState& state) {
    TDGIterAddable& st = state.back();
    const TAddDepIter& dep = st.Dep;

    if (st.IsInducedDep && dep.DepType == EDT_Include) {
        return EDepVerdict::No;
    }

    // Don't traverse ALL_SRCS subgraph twice
    if (auto* moduleBuilder = st.GetParentModuleBuilder(*this);
        moduleBuilder
        && moduleBuilder->AllSrcs.IsAllSrcsNode(st.Entry().AddCtx)
        && st.Entry().OnceEntered
    ) {
        return EDepVerdict::No;
    }

    auto ResolvedInputsChanged = [&st, this](TModAddData& moduleInfo, TModule& module) {
        Y_ASSERT(moduleInfo.InputsStatus == EInputsStatus::EIS_NotChecked);
        if (st.ResolvedInputChanged(YMake, module)) {
            YDebug() << "Module " << module.GetName() << " is reconfigured due to input resolution changes" << Endl;
            st.Entry().Props.SetIntentNotReady(EVI_GetModules, Graph.Names().FileConf.TimeStamps.CurStamp(), TPropertiesState::ENotReadyLocation::Custom);
            moduleInfo.InputsStatus = EInputsStatus::EIS_Changed;
        } else {
            moduleInfo.InputsStatus = EInputsStatus::EIS_NotChanged;
        }
    };

    if (IsModuleType(st.Node.NodeType)) {
        auto* module = YMake.Modules.Get(st.Node.ElemId);
        Y_ASSERT(module);
        auto moduleInfo = GetAddedModuleInfo(MakeDepFileCacheId(st.Node.ElemId));
        Y_ASSERT(moduleInfo);
        if (module->IsLoaded()) {

            bool isPeerdirDep = IsPeerdirDep(st.Node.NodeType, dep.DepType, dep.DepNode.NodeType);
            if (!isPeerdirDep && moduleInfo->InputsStatus == EInputsStatus::EIS_NotChecked) {
                ResolvedInputsChanged(*moduleInfo, *module);
            }
            if (moduleInfo->InputsStatus == EInputsStatus::EIS_Changed) {
                return EDepVerdict::No;
            }
            if (isPeerdirDep && moduleInfo->InputsStatus == EInputsStatus::EIS_Changed) {
                moduleInfo->InputsStatus = EInputsStatus::EIS_NotChanged;
            }
        }
        else {
            moduleInfo->InputsStatus = EInputsStatus::EIS_NotChanged;
        }
    }

    if (IsRecurseDep(st.Node.NodeType, dep.DepType, dep.DepNode.NodeType)) {
        RecurseQueue.AddEdge(st.Node, dep.DepNode, dep.DepType);
        return EDepVerdict::No;
    }

    if (IsSearchDirDep(st.Node.NodeType, dep.DepType, dep.DepNode.NodeType)) {
        YDIAG(GUpd) << "Delaying dep " << st.Node.NodeType << " " << Graph.ToString(st.Node)
                     << " -" << dep.DepType << "> "
                     << dep.DepNode.NodeType << " " << " " << dep.DepNode.ElemId << " "
                     << Graph.ToString(dep.DepNode) << Endl;
        DelayedSearchDirDeps.GetNodeDepsByType(st.Node, EDT_Search).Push(dep.DepNode.ElemId);
        return EDepVerdict::No;
    }

    if (dep.DepType == EDT_OutTogetherBack) {
        CurEnt->second.MultOut = true;
        return EDepVerdict::Delay;
    }

    if (dep.DepType == EDT_BuildFrom)
        CurEnt->second.HasBuildFrom = true;
    else if (dep.DepType == EDT_BuildCommand && IsFileType(st.Node.NodeType))
        CurEnt->second.HasBuildCmd = true;
    return EDepVerdict::Yes;
}

inline TUpdReiter::EDepVerdict TUpdReiter::AcceptDep(TState& state) {
    const auto& st = state.back();
    const auto& dep = st.Dep;

    if (st.IsInducedDep && dep.DepType == EDT_Include) {
        return EDepVerdict::No;
    }

    if (IsRecurseDep(st.Node.NodeType, dep.DepType, dep.DepNode.NodeType)) {
        return EDepVerdict::No;
    }

    if (IsSearchDirDep(st.Node.NodeType, dep.DepType, dep.DepNode.NodeType)) {
        return EDepVerdict::No;
    }

    if (dep.DepType == EDT_OutTogetherBack) {
        if (MainOutputAsExtra && dep.DepNode.NodeType == EMNT_NonParsedFile) {
            return EDepVerdict::No;
        }

        return EDepVerdict::Delay;
    }

    return EDepVerdict::Yes;
}

inline TUpdReiter::TUpdReiter(TUpdIter& parentIter)
    : TDepthDGIter<TUpdReIterSt>(parentIter.Graph, TNodeId::Invalid)
    , ParentIter(parentIter)
    , CurEnt(nullptr)
{
    MainOutputAsExtra = ParentIter.MainOutputAsExtra;
}

inline bool TUpdReiter::Enter(TState& state) {
    TUpdReIterSt& st = state.back();
    BINARY_LOG(Iter, NIter::TEnterEvent, Graph, st.Node, state.size() < 2 ? EDT_Last : (state.end() - 2)->Dep.DepType, EIterType::ReIter);
    NStats::StackDepthStats.SetMax(NStats::EStackDepthStats::UpdReIterMaxStackDepth, state.size());
    if (st.Node.NodeType == EMNT_Deleted)
        return false;

    TUpdReIterSt* prev = state.size() > 1 ? &state[state.size() - 2] : nullptr;

    if (prev) {
        prev->Entry().IsMultiModuleDir |= IsAtDir2MultiModulePropertyDep(state, Graph);

        if (prev != nullptr && prev->Dep.DepType == EDT_Property && st.Node.NodeType == EMNT_BuildCommand) {
            auto intentName = GetCmdName(Graph.GetCmdName(st.Node).GetStr()).Before('.');
            st.IsInducedDep = IntentByName(intentName, false) == EVI_InducedDeps;
        }

        prev->SetupReceiveFromChildIntents(prev->Entry(), prev->Node.NodeType, st.Node.NodeType, prev->Dep.DepType, MainOutputAsExtra);
    }

    YDIAG(IPUR) << "ReIter::Enter " << Graph.ToString(st.Node) << '\n';
    const auto id = MakeDepsCacheId(st.Node.NodeType, st.Node.ElemId);
    TUpdIter::TNodes::iterator i = ParentIter.Nodes.find(id);
    bool found = i != ParentIter.Nodes.end();
    if (!found || !i->second.OnceEntered || !i->second.Props.HasNotReadyIntents()) { // generally may not happen, but there are loops
        if (!found) {
            YDIAG(IPUR) << "Skip not found node\n";
        } else {
            YDIAG(IPUR) << "Skip node: OnceEntered=" << BoolToChar(i->second.OnceEntered) << ", PropsNotReady=" << i->second.Props.DumpNotReadyIntents() << '\n';
        }

        st.EntryPtr = CurEnt = found ? &*i : nullptr;
        return false;
    }

    bool fresh = !i->second.ReIterOnceEnt;
    if (fresh) {
        i->second.ReIterOnceEnt = true;
        EnteredNodes.push_back(i);
        st.EntryPtr = &*i; // used in functions
        st.Add = i->second.AddCtx;

        TIntents fetchIntents;
        if (!prev) {
            fetchIntents = i->second.Props.GetNotReadyIntents() & GetNonRuntimeIntents();
        } else {
            // Правильно было бы запрашивать только передаваемые через ребро intent-ы,
            // но тогда нужно сделать в одном обходе возможность входить в каждый
            // узел неколько раз (по количеству intent-ов).
            // fetchIntents = prev->GetFetchIntents() & prev->IntentsToReceiveFromChild();
            fetchIntents = i->second.Props.GetNotReadyIntents() & GetNonRuntimeIntents();
        }
        Y_ASSERT((fetchIntents & GetRuntimeIntents()).Empty());
        st.ResetFetchIntents(fetchIntents, TPropertiesIterState::ELocation::ReIterEnter);
        if (!st.HasIntentsToFetch()) {
            st.EntryPtr = CurEnt = &*i;
            return false;
        }

        if (st.NodeStart == TNodeId::Invalid) {
            if (!st.Add)
                st.Add = FindUnflushedNode(id);
            if (!st.Add || st.Add->NeedInit2) {
                const TDepGraph::TNodeRef node = Graph.GetNodeById(st.Node.NodeType, st.Node.ElemId);
                st.NodeStart = node.Id();
                st.Node = node.Value();
            }
        }

        YDIAG(IPUR) << "ReIter clears InducedDeps: remove " << st.DumpFetchIntents() << Endl;
        st.Entry().Props.ClearValues(st.GetFetchIntents());

        YDIAG(IPUR) << "ReIter fresh " << Graph.ToString(st.Node) << Endl;
        if (!st.Add) {
            YDIAG(IPUR) << "ReIter !st.Add " << Graph.ToString(st.Node) << Endl;
            if ((st.Node.NodeType == EMNT_BuildCommand || st.Node.NodeType == EMNT_Property) && (IsPropDep(state) || state.size() < 2)) {
                YDIAG(IPUR) << "Rescanning property " << Graph.ToString(st.Node) << Endl;
                st.NodeToProps(Graph, ParentIter.DelayedSearchDirDeps, nullptr);
                i->second.Props.SetAllIntentsReady(TPropertiesState::ENotReadyLocation::ReIterNodeToProps);
            }

            Y_ASSERT(!(st.Node.NodeType == EMNT_MakeFile && state.size() == 1 &&
                       ParentIter.State.back().IsAtDirMkfDep(EMNT_MakeFile) &&
                       (i->second.Props.IsIntentNotReady(EVI_GetModules))));
        }
    }

    st.EntryPtr = CurEnt = &*i;
    return fresh;
}

inline void TUpdReiter::Leave(TState& state) {
    BINARY_LOG(Iter, NIter::TLeaveEvent, Graph, state.back().Node, EIterType::ReIter);

    TUpdReIterSt& st = state.back();
    if (st.EntryPtr) {
        st.Entry().Props.SetIntentsReady(st.GetFetchIntents(), TPropertiesState::ENotReadyLocation::ReIterLeave);
    }

    LastType = st.Node.NodeType;
    LastElem = st.Node.ElemId;
}

inline void TUpdReiter::Left(TState& state) {
    TEntryPtr childEnt = CurEnt;
    TUpdReIterSt& st = state.back();
    CurEnt = st.EntryPtr;
    Y_SCOPE_EXIT(&graph=Graph, from=MakeDepsCacheId(LastType, LastElem), to=st.EntryPtr->first) {
        DEBUG_USED(from, to, graph);
        BINARY_LOG(Iter, NIter::TLeftEvent, graph, from, to, EIterType::ReIter);
    };
    if (childEnt) {
        YDIAG(IPUR) << "ReIter::Left " << Graph.ToStringByCacheId(childEnt->first) << ", IsMultiModuleDir=" << childEnt->second.IsMultiModuleDir << Endl;

        auto& currProps = CurEnt->second.Props;
        const auto& chldProps = childEnt->second.Props;
        if (chldProps.HasValues() && st.Dep.DepType != EDT_OutTogetherBack) {
            YDIAG(IPUR) << "ReIter passing some props from " << Graph.ToStringByCacheId(childEnt->first) << " to " << Graph.ToString(st.Node) << ": "
                         << "FromProps[ " << chldProps.DumpValues(Graph) << " ] "
                         << " Pass " << st.DumpReceiveIntents() << " Use " << currProps.DumpRequiredIntents()
                         << " Not ready " << chldProps.DumpNotReadyIntents() << Endl;
            auto restrictedProps = childEnt->second.GetRestrictedProps(st.Dep.DepType, ParentIter.GetPropsToUse({LastType, LastElem}));
            currProps.CopyProps(chldProps, st.IntentsToReceiveFromChild() & st.GetFetchIntents(), restrictedProps);
            YDIAG(IPUR) << "ReIter: after adding to " << Graph.ToString(st.Node) << ": "
                        << "Props[ " << currProps.DumpValues(Graph) << " ] " << Endl;
        }
    }
}

const TNodeAddCtx* TUpdReiter::FindUnflushedNode(TDepsCacheId cacheId) {
    if (!UnflushedFilled) {
        for (const auto& p : ParentIter.State) {
            if (p.Add) {
                ParentUnflushed[MakeDepsCacheId(p.Node.NodeType, p.Node.ElemId)] = p.Add.Get();
            }
            for (const auto& n : p.DelayedNodes) {
                ParentUnflushed[MakeDepsCacheId(n->NodeType, n->ElemId)] = n.Get();
            }
        }
        UnflushedFilled = true;
    }
    auto i = ParentUnflushed.find(cacheId);
    return i ? i->second : nullptr;
}

void TUpdIter::Rescan(TDGIterAddable& from) {
    TUpdReiter it(*this);
    it.Restart(from.Dep);
    BINARY_LOG(Iter, NIter::TRescanEvent, Graph, from.Dep.DepNode);
    while (it.Next(it)) {
    }
}

TNodeId TUpdIter::RecursiveAddStartTarget(EMakeNodeType type, ui32 elemId, TModule* module) {
    RecurseQueue.MarkReachable(TDepTreeNode(type, elemId));
    while (!RecurseQueue.Empty()) {
        const TDepTreeNode node = RecurseQueue.GetFront();
        RecurseQueue.Pop();

        YDIAG(ShowRecurses) << "Add start node " << Graph.ToString(node) << Endl;
        RecursiveAddNode(node.NodeType, node.ElemId, module);
    }
    return Graph.GetNodeById(type, elemId).Id();
}

TNodeId TUpdIter::RecursiveAddNode(EMakeNodeType type, const TStringBuf& name, TModule* module) {
    const ui64 id = Graph.Names().AddName(type, name);
    return RecursiveAddNode(type, id, module);
}

TNodeId TUpdIter::RecursiveAddNode(EMakeNodeType type, ui64 id, TModule* module) {
    ParentModule = module;
    //YDIAG(GUpd) << "RA+: " << name << " = " << id << "\n";
    TAddDepIter start(Graph, TAddDepDescr(EDT_Include /*use real?*/, type, id));

    TState tmpState;
    tmpState.swap(State); // temp. fix if someone calls RecursiveAddNode from inside
    Restart(start);
    while (Next(*this)) {
    }
    tmpState.swap(State);
    //YDIAG(GUpd) << "RA-: " << name << Endl;

    return LastNode;
}
