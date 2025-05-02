#pragma once

#include "add_iter_debug.h"
#include "conf.h"
#include "induced_props.h"
#include "node_debug.h"
#include "add_node_context.h"
#include "module_resolver.h"
#include "module_restorer.h"
#include "module_add_data.h"
#include "ymake.h"

#include <devtools/ymake/common/uniq_vector.h>
#include <devtools/ymake/compact_graph/legacy_iterator.h>
#include <devtools/ymake/compact_graph/nodes_queue.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/diag/stats.h>
#include <devtools/ymake/resolver/resolve_cache.h>
#include <devtools/ymake/resolver/resolve_ctx.h>

#include <util/generic/hash_set.h>
#include <util/generic/hide_ptr.h>
#include <util/generic/stack.h>
#include <util/generic/ylimits.h>


class TUpdIter;
struct TUpdEntryStats;
class TModuleDef;
class TModuleBuilder;
class TYMake;
class TGeneralParser;

struct TAddDepIter {
    TDepTreeNode DepNode;
    TNodeId DepNodeId;
    EDepType DepType;

    explicit TAddDepIter(TDepGraph& graph, TNodeId nodeId)
        : DepNode()
        , DepNodeId(TNodeId::Invalid)
        , DepType(EDT_Include)
        , Iterator(graph.Get(nodeId).Edges().begin())
        , End(Iterator.AtEnd())
    {
    }

    TAddDepIter(TDepGraph& graph, const TAddDepDescr& dep)
        : DepNode(dep.NodeType, dep.ElemId)
        , DepNodeId(TNodeId::Invalid)
        , DepType(dep.DepType)
        , Iterator(graph.Get(TNodeId::Invalid).Edges().begin())
        , End(Iterator.AtEnd())
    {
    }

    void Next() {
        End = Iterator.AtEnd();
        if (!End) {
            DepNode = (*Iterator).To().Value();
            DepNodeId = (*Iterator).To().Id();
            DepType = (*Iterator).Value();
            ++Iterator;
        }
    }

    bool AtEnd() const {
        return End;
    }

    void SetIterator(TDepTreeDepIter iterator) {
        Iterator = iterator;
        End = Iterator.AtEnd();
    }

private:
    TDepTreeDepIter Iterator;
    bool End;
};

struct TDelayedSearchDirDeps {
public:
    const THashMap<TDepsCacheId, TUniqVector<ui32>>& GetDepsByType(EDepType depType) const {
        Y_ASSERT(DepTypeToNodeDeps.contains(depType));
        return DepTypeToNodeDeps.at(depType);
    }
    THashMap<TDepsCacheId, TUniqVector<ui32>>& GetDepsByType(EDepType depType) {
        return DepTypeToNodeDeps[depType];
    }

    TUniqVector<ui32>& GetNodeDepsByType(TDepTreeNode node, EDepType depType) {
        return GetDepsByType(depType)[MakeDepsCacheId(node.NodeType, node.ElemId)];
    }

    void Flush(const TGeneralParser& parser, TDepGraph& graph) const;
private:
    using TNodeToDeps = THashMap<TDepsCacheId, TUniqVector<ui32>>;
    THashMap<EDepType, TNodeToDeps> DepTypeToNodeDeps;
};

TString DumpProps(const TDepGraph& graph, const TNodeProperties& props);

inline TIntents GetNonRuntimeIntents() {
    return TIntents{EVI_InducedDeps, EVI_CommandProps};
}

inline TIntents GetRuntimeIntents() {
    return TIntents{EVI_GetModules, EVI_ModuleProps};
}

class TPropertiesState : public TNodeDebugOnly {
public:
    using ENotReadyLocation = NDebugEvents::NIter::ENotReadyLocation;

    TPropertiesState(const TNodeDebugOnly& nodeDebug)
        : TNodeDebugOnly(nodeDebug)
        , Values_(nodeDebug)
    {
        TNotReadyLogger notReadyLogger{*this, ENotReadyLocation::Constructor};
        NotReadyIntents_ = GetNonRuntimeIntents();
    }

    void SetupRequiredIntents(EMakeNodeType nodeType) {
        RequiredIntents_ = CalcRequiredIntentsForNode(nodeType);
        BINARY_LOG(IPRP, NIter::TSetupRequiredIntents, DebugGraph, DebugNode, RequiredIntents_);
    }

    const TIntents& GetRequiredIntents() const {
        return RequiredIntents_;
    }

    bool HasRequiredIntents() const {
        return !RequiredIntents_.Empty();
    }

    bool IsIntentRequired(EVisitIntent intent) const {
        return RequiredIntents_.Has(intent);
    }

    TString DumpRequiredIntents() const {
        return RequiredIntents_.Dump();
    }

    bool IsIntentNewerThan(EVisitIntent intent, ui8 timestamp) const {
        return IntentTimeStamps_[intent] > timestamp;
    }

    void UpdateIntentTimestamp(EVisitIntent intent, const TPropertiesState& child) {
        UpdateIntentTimestamp(intent, child.IntentTimeStamps_[intent]);
    }

    void UpdateIntentTimestamp(EVisitIntent intent, ui8 timestamp) {
        ui8& currentTimestamp = IntentTimeStamps_[intent];
        if (currentTimestamp < timestamp) {
            currentTimestamp = timestamp;
        }
    }

    TString DumpIntentTimestamp(EVisitIntent intent) const {
        return ToString(static_cast<int>(IntentTimeStamps_[intent]));
    }

    void SetIntentNotReady(EVisitIntent intent, ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        NotReadyIntents_.Add(intent);
    }

    void SetIntentsNotReady(TIntents intents, ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        NotReadyIntents_.Add(intents);
    }

    void SetIntentNotReady(EVisitIntent intent, ui8 timestamp, ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        NotReadyIntents_.Add(intent);
        UpdateIntentTimestamp(intent, timestamp);
    }

    void SetIntentReady(EVisitIntent intent, ui8 timestamp, ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        NotReadyIntents_.Remove(intent);
        UpdateIntentTimestamp(intent, timestamp);
    }

    void SetIntentsReady(TIntents intents, ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        NotReadyIntents_.Remove(intents);
    }

    void SetIntentsReady(TIntents intents, ui8 timestamp, ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        FOR_ALL_INTENTS(intent) {
            if (intents.Has(intent)) {
                UpdateIntentTimestamp(intent, timestamp);
            }
        }
        NotReadyIntents_.Remove(intents);
    }

    void SetAllIntentsReady(ENotReadyLocation location) {
        TNotReadyLogger notReadyLogger{*this, location};
        NotReadyIntents_ = TIntents::None();
    }

    bool HasNotReadyIntents() const {
        return !NotReadyIntents_.Empty();
    }

    bool IsIntentNotReady(EVisitIntent intent) const {
        return NotReadyIntents_.Has(intent);
    }

    const TIntents& GetNotReadyIntents() const {
        return NotReadyIntents_;
    }

    TString DumpNotReadyIntents() const {
        return NotReadyIntents_.Dump();
    }

    bool HasValues() const {
        return !Values_.Empty();
    }

    const TPropsNodeList& GetValues(TPropertyType propType) const {
        AssertIntentIsReady(propType.GetIntent());

        const TPropsNodeList* values = FindValues(propType);
        Y_ASSERT(values);
        return *values;
    }

    using TPropTypeAndValues = std::pair<TPropertyType, const TPropsNodeList*>;

    TVector<TPropTypeAndValues> GetValues(EVisitIntent intent) const {
        TVector<TPropTypeAndValues> result{Reserve(16)};

        AssertIntentIsReady(intent);

        for (const auto& [type, values] : Values_) {
            if (type.GetIntent() == intent) {
                result.emplace_back(type, &values);
            }
        }

        return result;
    }

    const TPropsNodeList* FindValues(TPropertyType propType) const {
        AssertIntentIsReady(propType.GetIntent());

        auto it = Values_.Find(propType);
        if (it == Values_.end()) {
            return nullptr;
        }

        return &it->second;
    }

    void AddValues(TPropertyType propType, const TVector<TDepsCacheId>& values, TPropertySourceDebugOnly sourceDebug) {
        Values_.AddProps(propType, values, sourceDebug);
    }

    void AddSimpleValue(TPropertyType propType, TPropertySourceDebugOnly sourceDebug) {
        Values_.AddSimpleProp(propType, sourceDebug);
    }

    void SetValues(TPropertyType propType, TPropValues&& values, TPropertySourceDebugOnly sourceDebug) {
        Values_.SetPropValues(propType, std::move(values), sourceDebug);
    }

    void CopyProps(const TPropertiesState& from, TIntents copyIntents, TUsingRules skipPropTypes) {
        Values_.CopyProps(from.Values_, copyIntents, skipPropTypes);
    }

    void ClearValues() {
        Values_.Clear();
        // Практически все случаи вызова этого метода свидетельствуют о ситуации
        // сброса состояния узла. Пока более явного определения этой ситуации у нас нет.
        NotReadyIntents_ = GetNonRuntimeIntents();
    }

    void ClearValues(TIntents intents) {
        for (auto& [type, values] : Values_) {
            if (intents.Has(type.GetIntent())) {
                values.Clear();
            }
        }
    }

    TString DumpValues(const TDepGraph& graph) const {
        return DumpProps(graph, Values_);
    }

private:
    // Intent-ы, свойства которых применяются к самому узлу непосредственно
    // и могут влиять на его содержимое и зависимости.
    TIntents RequiredIntents_ = {};

    TIntents NotReadyIntents_ = {};

    ui8 IntentTimeStamps_[EVI_MaxId] = {};

    TNodeProperties Values_;

private:
    class TNotReadyLoggerBase {
    public:
        TNotReadyLoggerBase(TPropertiesState& state, ENotReadyLocation location)
            : State_(state), Location_(location)
        {
            PreviousIntents_ = state.NotReadyIntents_;
        }

        ~TNotReadyLoggerBase() {
            DEBUG_USED(State_, Location_);
            BINARY_LOG(IPRP, NIter::TNotReadyIntents, State_.DebugGraph, State_.DebugNode, PreviousIntents_, State_.NotReadyIntents_, Location_);
        }

    private:
        TPropertiesState& State_;
        TIntents PreviousIntents_;
        ENotReadyLocation Location_;
    };

    using TNotReadyLogger = TDebugOnly<TNotReadyLoggerBase>;

    void AssertIntentIsReady(EVisitIntent intent) const {
        DEBUG_USED(intent);

        // Известно, что этот assert срабатывает.
        // Включим после починки логики.
        return;

#if !defined(NDEBUG)
        if(NotReadyIntents_.Has(intent)) {
            YDIAG(IPRP)
                << "ASSERTION FAILED. Intent is not ready."
                << ' ' << DumpDebugNode()
                << " intent=[" << IntentToChar(intent) << ']'
                << Endl;
            Y_ASSERT(false);
        }
#endif
    }

    static TIntents CalcRequiredIntentsForNode(EMakeNodeType nodeType) {
        switch (nodeType) {
            case EMNT_NonParsedFile:
                return TIntents{EVI_InducedDeps, EVI_CommandProps};
            case EMNT_Directory:
                return TIntents{EVI_GetModules};
            case EMNT_Program: // IsModuleType
            case EMNT_Library:
            case EMNT_Bundle:
                return TIntents{EVI_ModuleProps};
            default:
                return TIntents::None();
        }
    }
};

struct TUpdEntryStats : public TNodeDebugOnly {
    union {
        ui32 AllFlags;
        struct {  // 30 bits used
            ui8 ModStamp;  // updated for real files (File and Makefile) only
            ui8 IncModStamp;
            bool OnceEntered : 1;
            bool InStack : 1;
            bool HasBuildFrom : 1;
            bool HasBuildCmd : 1;
            bool Reassemble : 1;  // usually means that AddCtx is pre-filled
            bool MultOut : 1;
            bool MarkedAsUnknown : 1;  // The node was processed but should be ignored until reentrance
            bool IsMultiModuleDir : 1;
            bool OnceProcessedAsFile : 1;
            bool HasChanges : 1;
            bool ReIterOnceEnt : 1;
            bool AddCtxOwned : 1;
            bool PassInducedIncludesThroughFiles : 1;
            bool PassNoInducedDeps : 1;
        };
    };

    TNodeAddCtx* AddCtx = nullptr;
    TPropertiesState Props;
    TModAddData ModuleData;

    TUpdEntryStats(bool inStack, const TNodeDebugOnly& nodeDebug);

    Y_FORCE_INLINE TNodeAddCtx& GetAddCtx(TModule* module, TYMake& yMake, bool isModule = false) {
        if (!AddCtx) {
            TFileHolder fileContent;
            AddCtx = new TNodeAddCtx(module, yMake, *this, isModule, nullptr, fileContent, EReadFileContentMethod::ON_DEMAND);
            AddCtxOwned = true;
        }
        return *AddCtx;
    }
    bool HasModule() const {
        return AddCtx && AddCtx->IsModule;
    }
    void DepsToInducedProps(TPropertyType propType, const TDeps& deps, TPropertySourceDebugOnly sourceDebug);

    TUsingRules GetRestrictedProps(EDepType depType, TUsingRules propsToUse) const;

    void SetReassemble(bool reassemble) {
        Reassemble = reassemble;
    }

    void SetOnceEntered(bool onceEntered) {
        OnceEntered = onceEntered;
    }

    ~TUpdEntryStats() {
        if (AddCtxOwned) {
            delete AddCtx;
        }
    }
};

class TPropertiesIterState : public TNodeDebugOnly {
public:
    using ELocation = NDebugEvents::NIter::EFetchIntentLocation;

    TPropertiesIterState(TNodeDebugOnly nodeDebugOnly) : TNodeDebugOnly(nodeDebugOnly) {}

    void SetupReceiveFromChildIntents(
        const TUpdEntryStats &parentState,
        EMakeNodeType prntNodeType,
        EMakeNodeType chldNodeType,
        EDepType edgeType,
        bool mainOutputAsExtra
    )
    {
        ReceiveFromChildIntents_ = CalcIntentsToReceiveFromChild(
            parentState, prntNodeType, chldNodeType, edgeType, mainOutputAsExtra
        );
        BINARY_LOG(IPRP, NIter::TSetupReceiveFromChildIntents, DebugGraph, DebugNode, chldNodeType, edgeType, ReceiveFromChildIntents_);
    }

    bool HasIntentsToReceiveFromChild() const {
        return !ReceiveFromChildIntents_.Empty();
    }

    const TIntents& IntentsToReceiveFromChild() const {
        return ReceiveFromChildIntents_;
    }

    bool ShouldReceiveIntentFromChild(EVisitIntent intent) const {
        return ReceiveFromChildIntents_.Has(intent);
    }

    TString DumpReceiveIntents() const {
        return ReceiveFromChildIntents_.Dump();
    }

    void ResetFetchIntents(TIntents intents, ELocation location) {
        FetchIntents_ = intents;
        DEBUG_USED(location);
        BINARY_LOG(IPRP, NIter::TResetFetchIntents, DebugGraph, DebugNode, FetchIntents_, location);
    }

    void ResetFetchIntents(bool parentHasChanges, const TPropertiesState& parentState, TPropertiesIterState& parentIterState) {
        TIntents requiredByParent = parentState.GetRequiredIntents();
        if (!parentHasChanges) {
            requiredByParent.Remove(GetNonRuntimeIntents());
        }
        FetchIntents_ = (parentIterState.FetchIntents_ & parentIterState.ReceiveFromChildIntents_) | requiredByParent;
        BINARY_LOG(IPRP, NIter::TResetFetchIntents, DebugGraph, DebugNode, FetchIntents_, ELocation::IterEnter);
    }

    bool ShouldFetchIntent(EVisitIntent intent) const {
        return FetchIntents_.Has(intent);
    }

    bool HasIntentsToFetch() const {
        return !FetchIntents_.Empty();
    }

    TIntents GetFetchIntents() const {
        return FetchIntents_;
    }

    TString DumpFetchIntents() const {
        return FetchIntents_.Dump();
    }

private:
    TIntents FetchIntents_ = {};
    TIntents ReceiveFromChildIntents_ = {}; // From the last 'Enter'-ed child

private:
    static TIntents CalcIntentsToReceiveFromChild(
        const TUpdEntryStats& parentState,
        EMakeNodeType prntNodeType,
        EMakeNodeType chldNodeType,
        EDepType edgeType,
        bool mainOutputAsExtra
    );
};

struct TUpdIterStBase: public TPropertiesIterState {
    typedef TAddDepIter DepIter;
    TDepTreeNode Node;
    TAddDepIter Dep;
    TNodeId NodeStart;
    ui64 CurDep;
    TUpdEntryPtr EntryPtr;
    ui8 MinNodeModTime;   // only for nodes that accept properties
    TStack<TAddDepIter> DelayedDeps;
    bool IsInducedDep = false;
    size_t ModulePosition = 0;

    //void SetIntent(const TUpdIterStBase &prev);
    bool NodeToProps(TDepGraph& graph, TDelayedSearchDirDeps& delayedDirs, const TNodeAddCtx* addCtx);
    explicit TUpdIterStBase(TDepGraph& graph, TNodeId nodeId)
        : TPropertiesIterState(TNodeDebugOnly{graph, nodeId})
        , Node(nodeId != TNodeId::Invalid ? graph.Get(nodeId).Value() : TDepTreeNode())
        , Dep(graph, nodeId)
        , NodeStart(nodeId)
        , CurDep(0)
        , EntryPtr(nullptr)
        , MinNodeModTime(0)
    {
    }

    TUpdEntryStats& Entry() {
        return EntryPtr->second;
    }

    const TUpdEntryStats& Entry() const {
        return EntryPtr->second;
    }

    bool IsAtDirMkfDep(EMakeNodeType depNodeType) const {
        return Node.NodeType == EMNT_Directory && Dep.DepType == EDT_Include && depNodeType == EMNT_MakeFile;
    }

    void DelayCurrentDep() {
        DelayedDeps.push(Dep);
    }

    bool HasDelayedDeps() const {
        return !DelayedDeps.empty();
    }

    TAddDepIter PopDelayedDep() {
        Y_ASSERT(HasDelayedDeps());
        auto res = DelayedDeps.top();
        DelayedDeps.pop();
        Dep.DepNode = res.DepNode;
        Dep.DepType = res.DepType;
        Dep.DepNodeId = res.DepNodeId;
        return res;
    }
};

struct TDGIterAddable: public TUpdIterStBase {
    TDepGraph& Graph;
    TAutoPtr<TNodeAddCtx> Add;

    class TDelayedNodes : public TVector<TAutoPtr<TNodeAddCtx>> {
    public:
        using TBase = TVector<TAutoPtr<TNodeAddCtx>>;
        TDelayedNodes() = default;
        TDelayedNodes(TDelayedNodes&& other) : TBase(other) {
            other.clear();
        }
    };

    TDelayedNodes DelayedNodes;
    bool WasFresh = false;

    TDGIterAddable(TDepGraph& graph, const TUpdIterStBase& base)
        : TUpdIterStBase(base)
        , Graph(graph)
    {
    }
    TDGIterAddable(TDepGraph& graph, TNodeId nodeId);
    TDGIterAddable(TDepGraph& graph, const TAddDepIter& it);
    TDGIterAddable(TDGIterAddable&&) = default;
    ~TDGIterAddable();
    Y_FORCE_INLINE void InitDeps() {
        //Y_ASSERT(!Dep.Ptr);
        if (Add && !Add->NeedInit2) {
            // No operations.
        } else {
            Dep.SetIterator(Graph.GetNodeById(Node.NodeType, Node.ElemId).Edges().begin());
        }
    }

    Y_FORCE_INLINE void NextDep() {
        YDIAG(GUpd) << "NextDep " << CurDep << Endl;
        if (Add && !Add->NeedInit2) {
            Dep = TAddDepIter(Graph, Add->Deps[CurDep]);
        } else {
            Dep.Next();
        }
    }

    Y_FORCE_INLINE bool AtEnd() const {
        if (Add && !Add->NeedInit2) {
            return CurDep >= Add->Deps.Size();
        } else {
            return Dep.AtEnd();
        }
    }

    bool NeedUpdate(TYMake& yMake, TFileHolder& fileContent, bool reassemble, TDGIterAddable& lastInStack, const TDGIterAddable* prevInStack, const TDGIterAddable* pprevInStack);
    void RepeatResolving(TYMake& ymake, TAddIterStack& stack, TFileHolder& fileContent);
    bool ResolvedInputChanged(TYMake& ymake, TModule& module);
    bool ModuleDirsChanged(TYMake& ymake, TModule& module);
    void StartEdit(TYMake& yMake, TUpdIter& dgIter);

    TModule* GetModuleForEdit(TDepGraph& graph, TUpdIter& dgIter, bool& isModule);
    TModule* EnterModule(TYMake& yMake);
    TModule* RestoreModule(TYMake& yMake) const;
    bool NeedModule(const TAddIterStack& stack) const;

    TModuleBuilder* GetParentModuleBuilder(TUpdIter& dgIter);

    Y_FORCE_INLINE bool IsEdited() const {
        return Add && !Add->NeedInit2;
    }

    Y_FORCE_INLINE TNodeAddCtx& GetAddCtx(TModule* module, TYMake& yMake, bool isModule = false) {
        Y_ASSERT(!EntryPtr->second.AddCtx);
        Y_ASSERT(!Add || !isModule); // or provide late filling of Module in Add
        // TODO: fix exception safety when isModule == true
        if (!Add) {
            TFileHolder fileContent;
            Add = new TNodeAddCtx(module, yMake, EntryPtr->second, isModule, nullptr, fileContent, EReadFileContentMethod::ON_DEMAND);
        }
        return *Add;
    }
    void Flush(TAddIterStack& stack) {
        if (Add && !Add->NeedInit2) {
            TNodeAddCtx* add = Add.Get();
            NodeStart = Add->Flush(stack, Add); // Add may be taken away here
            if (add->IsModule)
                add->LeaveModule();
        } else {
            if (WasFresh) {
                Y_ASSERT(NodeStart != TNodeId::Invalid);
                Graph.Get(NodeStart)->State.SetLocalChanges(false, false);
            }
        }
        for (auto& i : DelayedNodes) {
            if (i.Get()) {
                i->Flush(stack, i, true);
            }
        }
        DelayedNodes.clear();
    }

    void RegisterDelayed(TAutoPtr<TNodeAddCtx>& node) {
        for (const auto& el : DelayedNodes) {
            if (&el == &node) {
                return; //TAutoPtr is already in DelayedNodes
            }
            if (node.Get() == el.Get()) {
                Y_UNUSED(node.Release()); // There is already owner of node in the DelayedNodes
                return;
            }
        }
        DelayedNodes.push_back(node);
    }

    void ForceReassemble() {
        EntryPtr->second.SetReassemble(true);
        EntryPtr->second.HasChanges = true;
        CurDep = 0;
        InitDeps();
    }

    TString Print(const TDepGraph& graph) const {
        return graph.ToString(Node);
    }

    inline void SetupPropsPassing(TDGIterAddable* parentIterState, bool mainOutputAsExtra);

    void UseProps(TYMake& ymake, const TPropertiesState& props, TUsingRules restrictedProps);
    /// trick DGIter into processing additional nodes as current node's children
    void AddMoreDeps(const TVector<std::pair<EDepType, ui64>>& deps, IMemoryPool& pool, const TDepGraph& graph, bool ignFirst);
};

class TUpdIter: public TDepthDGIter<TDGIterAddable> {
public:
    TYMake& YMake;
    TNodesQueue RecurseQueue;
    TNodesQueue DependsQueue;
    TDelayedSearchDirDeps DelayedSearchDirDeps;

    THashMap<ui32, ui32> MainOutputId;
    THashMap<ui32, THashSet<TPropertyType>> PropsToUse;
    ui32 NeverCachePropId;

    mutable NStats::TUpdIterStats Stats{"UpdIter stats"};
    mutable NStats::TResolveStats ResolveStats{"Resolving stats"};
    mutable TResolveCaches ResolveCaches;

    TUpdIter(TYMake& yMake);

    struct TNodes
        : public THashMap<TDepsCacheId, TUpdEntryStats>
        , public TGraphDebugOnly
    {
        TNodes(const TDepGraph& graph) : TGraphDebugOnly{graph} {}
        iterator Insert(TDepsCacheId id, TYMake* yMake = nullptr, TModule* module = nullptr) {
            auto [i, _] = try_emplace(id, false, TNodeDebugOnly{*this, id});
            if (module) {
                Y_ASSERT(yMake);
                i->second.GetAddCtx(module, *yMake);
            }
            return i;
        }
    };

    TNodes Nodes;
    typedef ::TUpdEntryStats TEntryStats;
    typedef ::TUpdEntryPtr TEntryPtr;
    TEntryPtr CurEnt = nullptr;
    TNodeId LastNode = TNodeId::Invalid;
    ui32 LastElem = 0;
    EMakeNodeType LastType;
    TModule* ParentModule = nullptr;
    bool MainOutputAsExtra = false;

    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);
    EDepVerdict AcceptDep(TState& state);

    void NukeModuleDir(TState& state);

    void SaveModule(ui64 elemId, const TAutoPtr<TModuleDef>& mod);

    TNodeId RecursiveAddStartTarget(EMakeNodeType type, ui32 elemId, TModule* module);
    TNodeId RecursiveAddNode(EMakeNodeType type, const TStringBuf& name, TModule* module);
    TNodeId RecursiveAddNode(EMakeNodeType type, ui64 id, TModule* module);

    void Rescan(TDGIterAddable& from);

    // Retieve node representation during graph construction depending on
    // the graph being constructed
    TModAddData* GetAddedModuleInfo(TDepsCacheId cachedId) {
        const auto it = cachedId != TDepsCacheId::None ? Nodes.find(cachedId) : Nodes.end();
        if (it == Nodes.end()) {
            YDIAG(V) << "GetAddedModuleInfo: Node " << cachedId << " not found " << Endl;
            return nullptr;
        }
        return &it->second.ModuleData;
    }

    const TModAddData* GetAddedModuleInfo(TDepsCacheId cachedId) const {
         return const_cast<TUpdIter*>(this)->GetAddedModuleInfo(cachedId);
    }

    void RestorePropsToUse();

    TUsingRules GetPropsToUse(TDepTreeNode node) const;

    NGraphUpdater::ENodeStatus CheckNodeStatus(const TDepTreeNode& node) const;

    ~TUpdIter() override {
        // FIXME! This contains references into Nodes collection
        State.clear();
    }

private:
    bool DirectDepsNeedUpdate(const TDGIterAddable& st, const TDepTreeNode& oldDepNode);
    TGetPeerNodeResult GetPeerNodeIfNeeded(const TDGIterAddable& st);

    void PropagateIncDirs(const TDGIterAddable& st) const;
};

template <class V>
bool IsAtDirMkfDep(V& v) {
    Y_ASSERT(v.back().Node.NodeType == EMNT_MakeFile);
    return v.size() >= 2 && v[v.size() - 2].IsAtDirMkfDep(EMNT_MakeFile);
}

template <class V>
bool IsPropDep(V& v) {
    return v.size() >= 2 && v[v.size() - 2].Dep.DepType == EDT_Property;
}

template <class V>
bool IsAtDir2MultiModulePropertyDep(V& v, const TDepGraph& graph) {
    return v.size() >= 2 && IsDirType(v[v.size() - 2].Node.NodeType) && IsPropDep(v) && IsPropertyTypeNode(v.back().Node.NodeType) && GetPropertyName(graph.GetCmdName(v.back().Node).GetStr()) == NProps::MULTIMODULE;
}

/// Property of a directory or makefile
inline bool IsNonmodulePropertyDep(const TDGIterAddable& st) {
    return (IsDirType(st.Node.NodeType) || st.Node.NodeType == EMNT_MakeFile) &&
           st.Dep.DepType == EDT_Property &&
           (st.Dep.DepNode.NodeType == EMNT_BuildCommand || st.Dep.DepNode.NodeType == EMNT_Property);
}

/// Inside property:
/// [*]--(EDT_Property)-->[EMNT_Property|EMNT_BuildCommand] --(*)--> [We are here]
template<class V>
bool IsPropertyValue(const V& stack) {
    size_t sz = stack.size();
    if (sz >= 3) {
        const TDGIterAddable& prev = stack[sz - 2];
        return stack[sz - 3].Dep.DepType == EDT_Property && (prev.Node.NodeType == EMNT_BuildCommand || prev.Node.NodeType == EMNT_Property);
    }
    return false;
}

struct TUpdReIterSt: public TUpdIterStBase {
    const TNodeAddCtx* Add;
    TDepGraph& Graph;

    TUpdReIterSt(TDepGraph& graph, TNodeId nodeId)
        : TUpdIterStBase(graph, nodeId)
        , Add(nullptr)
        , Graph(graph)
    {
        NodeStart = nodeId;
        Node = Graph.Get(NodeStart).Value();
    }

    TUpdReIterSt(TDepGraph& graph, const TAddDepIter& it)
        : TUpdIterStBase(graph, it.DepNodeId)
        , Add(nullptr)
        , Graph(graph)
    {
        NodeStart = it.DepNodeId != TNodeId::Invalid ? it.DepNodeId : graph.GetNodeById(it.DepNode).Id();
        if (NodeStart != TNodeId::Invalid) {
            Node = Graph.Get(NodeStart).Value();
        } else {
            Node = it.DepNode;
        }
#if defined(YMAKE_DEBUG)
        DebugNode = MakeDepsCacheId(Node.NodeType, Node.ElemId);
#endif
        YDIAG(GUpd) << "new IterAddable " << graph.ToString(Node) << Endl;
    }

    Y_FORCE_INLINE void InitDeps() {
        //Y_ASSERT(!Dep.Ptr);
        if (Add && !Add->NeedInit2) {
            // No operations.
        } else {
            Dep.SetIterator(Graph.GetNodeById(Node.NodeType, Node.ElemId).Edges().begin());
        }
    }

    Y_FORCE_INLINE void NextDep() {
        YDIAG(GUpd) << "NextDep " << CurDep << Endl;
        if (Add && !Add->NeedInit2) {
            Dep = TAddDepIter(Graph, Add->Deps[CurDep]);
        } else {
            Dep.Next();
        }
    }

    Y_FORCE_INLINE bool AtEnd() const {
        if (Add && !Add->NeedInit2) {
            return CurDep >= Add->Deps.Size();
        } else {
            return Dep.AtEnd();
        }
    }

    TString Print(const TDepGraph& graph) const {
        return graph.ToString(Node);
    }
};

// This class is supposed to help iterating the graph at times when
// some nodes are still not flushed by TUpdIter (mostly due to cycles).
// Some things like IncModStamp are not calculated here because we suppose that we
// have already visited the subtree before using ReIter.
struct TUpdReiter: public TDepthDGIter<TUpdReIterSt> {
    TUpdIter& ParentIter;
    typedef TUpdEntryStats TEntryStats;
    TVector<TUpdIter::TNodes::iterator> EnteredNodes; // clear list for ReIterOnceEnt
    typedef ::TUpdEntryPtr TEntryPtr;
    TEntryPtr CurEnt;
    THashMap<TDepsCacheId, const TNodeAddCtx*> ParentUnflushed;
    bool UnflushedFilled = false;
    EMakeNodeType LastType;
    ui32 LastElem = 0;
    bool MainOutputAsExtra = false;

    TUpdReiter(TUpdIter& parentIter);

    ~TUpdReiter() override {
        Clear();
    }

    void Clear() {
        for (TVector<TUpdIter::TNodes::iterator>::iterator j = EnteredNodes.begin(); j != EnteredNodes.end(); ++j)
            (*j)->second.ReIterOnceEnt = false;
        EnteredNodes.clear();
    }

    bool Enter(TState& state);
    void Leave(TState& state);
    void Left(TState& state);
    EDepVerdict AcceptDep(TState& state);

    const TNodeAddCtx* FindUnflushedNode(TDepsCacheId cacheId);
};
