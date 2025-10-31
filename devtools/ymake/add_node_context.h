#pragma once

#include "module_add_data.h"
#include "module_state.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/graph.h>

#include <devtools/ymake/symbols/symbols.h>

class TUpdIter;
struct TUpdEntryStats;
class TModuleDef;
class TModuleBuilder;
class TYMake;
struct TNodeAddCtx;

struct TCreateParsedInclsResult {
    enum EStatus {
        Nothing,
        Existing,
        Changed,
    };
    TNodeAddCtx* Node;
    EStatus Status;
};

enum class EReadFileContentMethod : bool {
    FORCED,
    ON_DEMAND
};

class TFlushState {
public:
    size_t FlushPos = 0;
    TNodeId NodeId;

    TFlushState(TDepGraph& graph, TNodeId oldNodeId, EMakeNodeType newNodeType);
    void FinishFlush(TDepGraph& graph, TDepGraph::TNodeRef node);

private:
    THashSet<std::pair<TNodeId, EDepType>> OldEdges_;
    bool CheckEdges_ = false;
    bool IsNewNode = false;

    bool AreEdgesChanged(TDepGraph::TConstNodeRef nodeRef) const;

    static bool ShouldCheckEdgesChanged(EMakeNodeType nodeType) {
        return (nodeType == EMNT_File || nodeType == EMNT_MissingFile);
    }

    static bool ShouldCheckContentChanged(EMakeNodeType nodeType) {
        return (nodeType == EMNT_File || nodeType == EMNT_MakeFile);
    }
};

struct TNodeAddCtx : public TAddDepAdaptor {
    TModule* Module;
    TModuleDef* ModuleDef = nullptr;
    TModuleBuilder* ModuleBldr = nullptr;
    TYMake& YMake;
    TDepGraph& Graph;
    TUpdIter& UpdIter;
    std::reference_wrapper<TUpdEntryStats> Entry;
    bool IsModule; // this also means that we own Module object
    bool NeedInit2;
    bool PartEdit = false;
    TDeps Deps;
    const TIndDepsRule* DepsRule = nullptr;
    bool DepsRuleSet = false;
    bool FlushDone = false;
    TNodeAddCtx* Action = nullptr;

    THolder<TFlushState> FlushState;

    // when constructor is called with stack == NULL, separate call to Init2 is necessary to complete the initialization
    TNodeAddCtx(TModule* module, TYMake& yMake, TUpdEntryStats& entry, bool isMod, TAddIterStack* stack, TFileHolder& fileContent, EReadFileContentMethod readMethod);
    void Init2(TAddIterStack& stack, TFileHolder& fileContent, TModule* mod, EReadFileContentMethod readMethod);
    ~TNodeAddCtx() override;

    void WriteHeader();

public:
    // these are in add_node_context_inline.h
    void AddDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName);
    void AddDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId);

    void AddDepIface(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) final;
    void AddDepIface(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) final;

    bool AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) final;
    bool AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) final;

    void AddDeps(const TDeps& deps) final {
        Deps.Add(deps);
    }

    void AddDepsUnique(const struct TPropsNodeList& what, EDepType depType, EMakeNodeType nodeType);

    bool HasAnyDeps() const final {
        return !Deps.Empty();
    }

    // ATTN! quite expensive;
    // NOTE: numFirst == 0 -> no limit
    void GetOldDeps(TDeps& deps, size_t numFirst, bool allOutputs);
    void UseOldDeps();

    void AddInputs();

    void InitDepsRule();
    const TIndDepsRule* SetDepsRuleByName(TStringBuf name) final;

    void RemoveIncludeDeps(ui64 startFrom);

    TCreateParsedInclsResult CreateParsedIncls(TStringBuf type, const TVector<TResolveFile>& files);
    static TCreateParsedInclsResult CreateParsedIncls(
        TModule* module, TDepGraph& graph, TUpdIter& updIter, TYMake& yMake,
        EMakeNodeType cmdNodeType, ui64 cmdElemId,
        TStringBuf type, const TVector<TResolveFile>& files);
    void AddParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) final;
    void AddDirsToProps(const TDirs& dirs, TStringBuf propName) final;
    void AddDirsToProps(const TVector<ui32>& dirIds, TStringBuf propName) final;
    void AddDirsToProps(const TPropsNodeList& props, TStringBuf propName) final;

    inline TPropertiesState& GetProps() final;

    TUpdEntryStats& GetEntry() {
        return Entry.get();
    }

    inline TAddDepAdaptor& AddOutput(ui64 fileId, EMakeNodeType defaultType, bool addToOwn = true) final;

    void UpdCmdStamp(TNameDataStore<TCommandData, TCmdView>& conf, TTimeStamps& stamps, bool changed);
    void UpdCmdStampForNewCmdNode(TNameDataStore<TCommandData, TCmdView>& conf, TTimeStamps& stamps, bool changed);

    TNodeId Flush(TAddIterStack& stack, TAutoPtr<TNodeAddCtx>& me, bool lastTry = false);
    void LeaveModule();
    void NukeModule();

    bool IsDepDeleted(const TAddDepDescr& dep) const;
    void DeleteDep(size_t idx);

    TModAddData& GetModuleData() final;
private:
    void SetDepsRule(const TIndDepsRule* rule) {
        DepsRule = rule;
        DepsRuleSet = true;
    }
};

// This pseudo-node stores all incoming information and can be later:
// a) discarded without any side effects;
// b) flushed to a real node through TAddDepAdaptor interface.
class TMaybeNodeUpdater : public TAddDepAdaptor {
private:
    struct TAddParsedInclsRequest {
        TString Type;
        TVector<TResolveFile> Includes;
    };

    struct TAddDirsToPropsRequest {
        TString Type;
        TVector<ui32> Dirs;
    };

    enum class ERequestType {
        SingleDep,
        Includes,
        Dirs
    };

private:
    TUniqDeque<TAddDepDescr> Deps;
    TVector<TAddParsedInclsRequest> ParsedIncls;
    TVector<TAddDirsToPropsRequest> ParsedDirs;

    TVector<ERequestType> SavedRequests;

    TSymbols& Names;

public:
    TMaybeNodeUpdater(TSymbols& names)
        : Names(names)
    {
    }

    bool AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, ui64 elemId) final;
    bool AddUniqueDep(EDepType depType, EMakeNodeType elemNodeType, TStringBuf elemName) final;
    bool HasAnyDeps() const final;
    void AddParsedIncls(TStringBuf type, const TVector<TResolveFile>& files) final;
    void AddDirsToProps(const TDirs& dirs, TStringBuf propName) final;
    void AddDirsToProps(const TVector<ui32>& dirIds, TStringBuf propName) final;
    void AddDirsToProps(const TPropsNodeList& props, TStringBuf propName) final;

    TDepRef AddDep(EDepType, EMakeNodeType, TStringBuf) {
        ythrow TNotImplemented() << "AddDep: Not implemented in TMaybeNodeUpdater";
    }

    TDepRef AddDep(EDepType, EMakeNodeType, ui64) {
        ythrow TNotImplemented() << "AddDep: Not implemented in TMaybeNodeUpdater";
    }

    void AddDepIface(EDepType, EMakeNodeType, TStringBuf) final {
        ythrow TNotImplemented() << "AddDep: Not implemented in TMaybeNodeUpdater";
    }

    void AddDepIface(EDepType, EMakeNodeType, ui64) final {
        ythrow TNotImplemented() << "AddDep: Not implemented in TMaybeNodeUpdater";
    }

    void AddDeps(const TDeps&) final {
        ythrow TNotImplemented() << "AddDeps: Not implemented in TMaybeNodeUpdater";
    }

    TAddDepAdaptor& AddOutput(ui64, EMakeNodeType, bool) final {
        ythrow TNotImplemented() << "AddOutput: Not implemented in TMaybeNodeUpdater";
    }

    TPropertiesState& GetProps() final {
        ythrow TNotImplemented() << "GetProps: Not implemented in TMaybeNodeUpdater";
    }

    TModAddData& GetModuleData() final {
        ythrow TNotImplemented() << "GetModuleData: Not implemented in TMaybeNodeUpdater";
    }

    const TIndDepsRule* SetDepsRuleByName(TStringBuf) final {
        ythrow TNotImplemented() << "SetDepsRuleByName: Not implemented in TMaybeNodeUpdater";
    }

    bool HasChangesInDeps(TConstDepNodeRef otherNode) const;
    void UpdateNode(TAddDepAdaptor& node) const;
};

struct TAddDepContext {
    TAddDepContext(TAddDepAdaptor& node, TModule& module, TDepGraph& graph, TYMake& ymake, TUpdIter& iter, const TIndDepsRule* rule)
        : Node(node)
        , Module(module)
        , Graph(graph)
        , YMake(ymake)
        , UpdIter(iter)
        , DepsRule(rule)
        , UpdateReresolveCache(false)
    {
    }

    TAddDepContext(TNodeAddCtx& node, bool updateReresolveCache)
        : Node(node)
        , Module(*node.Module)
        , Graph(node.Graph)
        , YMake(node.YMake)
        , UpdIter(node.UpdIter)
        , DepsRule(node.DepsRule)
        , UpdateReresolveCache(updateReresolveCache)
    {
    }

    TAddDepAdaptor& Node;
    TModule& Module;
    TDepGraph& Graph;
    TYMake& YMake;
    TUpdIter& UpdIter;
    const TIndDepsRule* DepsRule;
    bool UpdateReresolveCache;
};
