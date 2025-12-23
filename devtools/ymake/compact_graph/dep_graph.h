#pragma once

#include "dep_types.h"
#include "graph.h"

#include <devtools/ymake/symbols/symbols.h>

#include <library/cpp/remmap/remmap.h>
#include <library/cpp/packedtypes/longs.h>

#include <util/generic/hash.h>
#include <util/ysaveload.h>

class TNodeState {
public:
    enum EChangedFlagsScope : ui8 {
        NotKnown,

        // Changes are known only for the node.
        //
        // There is also a case when "Local" scope corresponds to
        // the partially collected "Recursive" changes.
        // This can be only while we do the changes propagation.
        // And only when node processing is not yet done:
        // the node is itself in the iterator stack or it belongs to a loop,
        // some nodes of which are in the iterator stack.
        Local,

        // Changes are known for the node and all its descendants.
        Recursive
    };

    void SetLocalChanges(bool structureChanged, bool contentChanged) {
        // This assert triggers. There are multiple Flush calls for the same node.
        // Y_ASSERT(ChangedScope_ == NotKnown);
        // StructureChanged_ = structureChanged;
        // ContentChanged_ = contentChanged;
        Y_ASSERT(ChangedScope_ == NotKnown || ChangedScope_ == Local);
        ChangedScope_ = Local;
        UpdateChanges(structureChanged, contentChanged);
    }

    void UpdateLocalChanges(bool structureChanged, bool contentChanged) {
        Y_ASSERT(ChangedScope_ == Local);
        UpdateChanges(structureChanged, contentChanged);
    }

    void PropagateChangesFrom(const TNodeState& childNodeState, EChangedFlagsScope expectedScope = Local) {
        Y_ASSERT(ChangedScope_ == expectedScope && childNodeState.ChangedScope_ == Recursive);
        Y_UNUSED(expectedScope);
        UpdateChanges(childNodeState.StructureChanged_, childNodeState.ContentChanged_);
    }

    void PropagatePartialChangesFrom(const TNodeState& childNodeState) {
        Y_ASSERT(ChangedScope_ == Local && childNodeState.ChangedScope_ == Local);
        UpdateChanges(childNodeState.StructureChanged_, childNodeState.ContentChanged_);
    }

    EChangedFlagsScope GetChangedFlagsScope() const {
        return ChangedScope_;
    }

    void SetChangedFlagsRecursiveScope() {
        Y_ASSERT(ChangedScope_ == TNodeState::Local);
        ChangedScope_ = Recursive;
    }

    bool HasNoChanges() const {
        return ChangedScope_ == Recursive && !ContentChanged_ && !StructureChanged_;
    }

    bool HasRecursiveContentChanges() const {
        Y_ASSERT(ChangedScope_ == TNodeState::Recursive);
        return ContentChanged_;
    }

    bool HasRecursiveStructuralChanges() const {
        Y_ASSERT(ChangedScope_ == TNodeState::Recursive);
        return StructureChanged_;
    }

    bool GetReachable() const {
        return Reachable_;
    }

    void SetReachable(bool isReachable = true) {
       Reachable_ = isReachable;
    }

private:
    // Flag if the node is reachable from the current set of start targets.
    // This flag is stored in cache (see note for Load/Save methods of
    // TDepTreenode below.
    bool Reachable_ : 1 = false;

    // File content was changed.
    bool ContentChanged_ : 1 = false;

    // Structure was changed (nodes or edges was added, changed or removed).
    bool StructureChanged_ : 1 = false;

    EChangedFlagsScope ChangedScope_ : 2 = NotKnown;

    void UpdateChanges(bool structureChanged, bool contentChanged) {
        StructureChanged_ |= structureChanged;
        ContentChanged_ |= contentChanged;
    }
};

static_assert(sizeof(TNodeState) == 1);

class TDepTreeNode {
public:
    TNodeState State = {};

    EMakeNodeType NodeType;

    // id of associated data with kind of sort of user-defined semantics;
    // exact data type and semantics are determined by NodeType subsets (file-like, command-like);
    // uniqueness scope: a NodeType subset within a graph;
    // zero ids are reserved/invalid
    ui32 ElemId;

    TDepTreeNode(EMakeNodeType nodeType, ui32 elemId)
        : NodeType(nodeType)
        , ElemId(elemId)
    {
    }

    TDepTreeNode()
        : TDepTreeNode(EMNT_Deleted, 0)
    {
    }

    bool operator==(const TDepTreeNode& node) const {
        return node.NodeType == NodeType && node.ElemId == ElemId;
    }

    bool operator<(const TDepTreeNode& node) const {
        if (NodeType != node.NodeType) {
            return NodeType < node.NodeType;
        }
        return ElemId < node.ElemId;
    }

    // "State" is a non-persistent dependency graph node state
    // related to the current graph instance.
    // It should not be saved and should be default initialized
    // when the new graph instance created or loaded.
    // Note: The only exception to the written above is Reachable_ field
    // of "State". Reachable_ flag shares the byte for NodeType in cache.
    // Currently there is no need to save Reachable_ field of "State" in
    // a separate chunk of data in cache.

    void Load(IInputStream* input) {
        ui8 data;
        ::Load(input, data);
        NodeType = static_cast<EMakeNodeType>(0x7f & data);
        State.SetReachable(0x80 & data);
        ::Load(input, ElemId);
    }

    void Save(IOutputStream* output) const {
        ui8 data = static_cast<ui8>(NodeType) | static_cast<ui8>(State.GetReachable()) << 7;
        ::Save(output, data);
        ::Save(output, ElemId);
    }
};

static_assert(sizeof(TDepTreeNode) == 8);

inline TDepsCacheId MakeDepsCacheId(const TDepTreeNode& node) {
    return MakeDepsCacheId(node.NodeType, node.ElemId);
}

template <>
struct THash<TDepTreeNode> {
    size_t operator()(const TDepTreeNode& node) const {
        return CombineHashes(node.ElemId, static_cast<ui32>(node.NodeType));
    }
};

template <typename H>
H AbslHashValue(H h, const TDepTreeNode& node) {
  return H::combine(std::move(h), node.ElemId, node.NodeType);
}

using TNodeRelocationMap = THashMap<TDepsCacheId, TDepTreeNode>;

template <>
inline TDepTreeNode Deleted<TDepTreeNode>(void) {
    return TDepTreeNode();
}

template <>
inline bool Deleted(TDepTreeNode node) {
    return node.NodeType == EMNT_Deleted;
}

struct TNodeData {
    union {
        ui16 AllFlags = 0; // 10 bits used
        struct {
            ui8 NodeModStamp;      // for correct processing of induced deps

            ui8 PassInducedIncludesThroughFiles : 1;
            ui8 PassNoInducedDeps : 1;
        };
    };
    Y_SAVELOAD_DEFINE(AllFlags);
};

static_assert(sizeof(TNodeData) == sizeof(ui16), "union part of TNodeData must fit 16 bit");

class TDepGraph: public TCompactGraph<EDepType, TDepTreeNode, TCompactEdge<EDepType, 28>> {
private:
    TSymbols& Names_;

    using TId2NodeMap = THashMap<ui32, TNodeId, TIdentity>;
    TId2NodeMap MainId2NodeMap;
    TId2NodeMap CmdId2NodeMap;

    THashMap<ui32, TNodeData> NodeData; // TODO/FIXME: this is supposed to be file-only, but AddNode disagrees

public:
    using TBase = TCompactGraph<EDepType, TDepTreeNode, TCompactEdge<EDepType, 28>>;
    static constexpr const TNodeId MaxNodeId = TNodeId{(1 << 28) - 1};

    explicit TDepGraph(TSymbols& names)
        : Names_(names)
    {
        ResetId2NodeMaps();
    }

    THashMap<ui32, TNodeData>& GetFileNodeData() {
        return NodeData;
    }

    TNodeData& GetFileNodeData(ui32 elemId) {
        return NodeData[elemId];
    }

    TSymbols& Names() {
        return Names_;
    }

    const TSymbols& Names() const {
        return Names_;
    }

    // we just know it's a file-type node, don't need specific type
    TNodeRef GetFileNodeById(ui32 id) {
        Y_ASSERT(HasId2NodeMaps());

        const auto it = MainId2NodeMap.find(id);
        if (!it) {
            return GetInvalidNode();
        }
        auto ret = Get(it->second);
        if (!ret.IsValid()) {
            return GetInvalidNode();
        } else {
            return ret;
        }
    }
    TConstNodeRef GetFileNodeById(ui32 id) const {
        return const_cast<TDepGraph*>(this)->GetFileNodeById(id);
    }

    // we just know it's a command-type node, don't need specific type
    TNodeRef GetCommandNodeById(ui32 id) {
        Y_ASSERT(HasId2NodeMaps());

        const auto it = CmdId2NodeMap.find(id);
        if (!it) {
            return GetInvalidNode();
        }
        auto ret = Get(it->second);
        if (!ret.IsValid()) {
            return GetInvalidNode();
        } else {
            return ret;
        }
    }
    TConstNodeRef GetCommandNodeById(ui32 id) const {
        return const_cast<TDepGraph*>(this)->GetCommandNodeById(id);
    }

    TNodeRef GetNodeById(EMakeNodeType type, ui32 id) {
        if (type == EMNT_Deleted) {
            return GetInvalidNode();
        }
        Y_ASSERT(HasId2NodeMaps());

        if (UseFileId(type)) {
            return GetFileNodeById(id);
        } else {
            return GetCommandNodeById(id);
        }
    }

    TNodeRef GetNodeById(const TDepTreeNode& node) {
        return GetNodeById(node.NodeType, node.ElemId);
    }

    TConstNodeRef GetNodeById(EMakeNodeType type, ui32 id) const {
        return const_cast<TDepGraph*>(this)->GetNodeById(type, id);
    }

    TConstNodeRef GetNodeById(const TDepTreeNode& node) const {
        return GetNodeById(node.NodeType, node.ElemId);
    }

    TNodeRef GetFileNode(TStringBuf fname) {
        return GetFileNode(Names().FileConf.GetIdNx(fname));
    }

    TNodeRef GetFileNode(TFileView fname) {
        return GetFileNode(fname.GetElemId());
    }

    TConstNodeRef GetFileNode(TStringBuf fname) const {
        return const_cast<TDepGraph*>(this)->GetFileNode(fname);
    }

    TConstNodeRef GetFileNode(TFileView fname) const {
        return const_cast<TDepGraph*>(this)->GetFileNode(fname);
    }

    TNodeRef GetCommandNode(TStringBuf cname) {
        ui32 cid = Names().CommandConf.GetIdNx(cname);
        if (cid != 0) {
            return GetCommandNodeById(cid);
        }
        return GetInvalidNode();
    }
    TConstNodeRef GetCommandNode(TStringBuf cname) const {
        return const_cast<TDepGraph*>(this)->GetCommandNode(cname);
    }

    TNodeRef GetNode(EMakeNodeType type, TStringBuf name) {
        if (type == EMNT_Deleted) {
            return GetInvalidNode();
        }

        Y_ASSERT(HasId2NodeMaps());

        if (UseFileId(type)) {
            return GetFileNode(name);
        } else {
            return GetCommandNode(name);
        }
    }
    TConstNodeRef GetNode(EMakeNodeType type, TStringBuf name) const {
        return const_cast<TDepGraph*>(this)->GetNode(type, name);
    }

    /// @brief Get validated mutable reference to node
    /// For invalid return node exception will be thrown
    TNodeRef GetValidNode(EMakeNodeType type, TStringBuf name) {
        auto res = GetNode(type, name);
        if (!res.IsValid()) {
            ythrow yexception() << "Node for " << name << " is not available or deleted";
        }
        return res;
    }

    /// @brief Get validated mutable reference to node
    /// For invalid return node exception will be thrown
    TConstNodeRef GetValidNode(EMakeNodeType type, TStringBuf name) const {
        auto res = GetNode(type, name);
        if (!res.IsValid()) {
            ythrow yexception() << "Node for " << name << " is not available or deleted";
        }
        return res;
    }

    const TFileView GetFileName(EMakeNodeType nodeType, ui32 elemId) const { // slow - for rare messages & debug
        Y_ASSERT(UseFileId(nodeType));
        return Names().FileNameById(elemId);
    }

    const TCmdView GetCmdName(EMakeNodeType nodeType, ui32 elemId) const { // slow - for rare messages & debug
        Y_ASSERT(!UseFileId(nodeType));
        return Names().CmdNameById(elemId);
    }

    const TFileView GetFileName(const TDepTreeNode& node) const { // slow - for rare messages & debug
        Y_ASSERT(UseFileId(node.NodeType));
        return GetFileName(node.ElemId);
    }

    const TCmdView GetCmdName(const TDepTreeNode& node) const { // slow - for rare messages & debug
        Y_ASSERT(!UseFileId(node.NodeType));
        return GetCmdName(node.ElemId);
    }

    TString ToString(const TDepTreeNode& node) const {
        if (UseFileId(node.NodeType)) {
            TString fileName;
            GetFileName(node).GetStr(fileName);
            return fileName;
        }
        return TString{GetCmdName(node).GetStr()};
    }

    TStringBuf ToTargetStringBuf(const TDepTreeNode& node) const {
        if (UseFileId(node.NodeType)) {
            return GetFileName(node).GetTargetStr();
        }
        return GetCmdName(node).GetStr();
    }

    TStringBuf ToTargetStringBuf(TNodeId nodeId) const {
        return ToTargetStringBuf(Get(nodeId));
    }

    const TFileView GetFileNameByCacheId(TDepsCacheId cacheId) const {
        return Names().FileNameByCacheId(cacheId);
    }

    const TCmdView GetCmdNameByCacheId(TDepsCacheId cacheId) const {
        return Names().CmdNameByCacheId(cacheId);
    }

    TString ToStringByCacheId(TDepsCacheId cacheId) const {
        TStringStream out;
        if (IsFile(cacheId)) {
            out << GetFileNameByCacheId(cacheId);
        } else {
            out << GetCmdNameByCacheId(cacheId);
        }
        out.Finish();
        return out.Str();
    }

    const TFileView GetFileName(ui32 elemId) const { // slow - for rare messages & debug
        return Names().FileNameById(elemId);
    }

    const TCmdView GetCmdName(ui32 elemId) const { // slow - for rare messages & debug
        return Names().CmdNameById(elemId);
    }

    EMakeNodeType GetType(const TConstNodeRef& node) const {
        return node->NodeType;
    }

    EMakeNodeType GetType(TNodeId node) const {
        return Get(node)->NodeType;
    }

    TNodeRef AddNode(EMakeNodeType type, ui32 elemId) {
        auto foundNode = GetNodeById(type, elemId);
        if (foundNode.IsValid()) {
            Y_ASSERT(foundNode->NodeType == type);
            return foundNode;
        }
        auto node = TBase::AddNode({type, elemId});
        SyncNameId(node);
        if (elemId) {
            NodeData.insert({elemId, TNodeData{}});
        }
        return node;
    }

    TNodeRef AddNode(EMakeNodeType type, TStringBuf name) {
        ui32 elemId = Names().AddName(type, name);
        return AddNode(type, elemId);
    }

    void RelocateNodes(const TNodeRelocationMap& relocated){
        Y_ASSERT(HasId2NodeMaps());

        THashMap<TNodeId, TNodeId> replaces(relocated.size());
        for (const auto& relocation: relocated) {
            Y_ASSERT(IsFile(relocation.first) == UseFileId(relocation.second.NodeType));
            const auto& id2NodeMap = UseFileId(relocation.second.NodeType)? MainId2NodeMap : CmdId2NodeMap;

            auto originalElemId = ElemId(relocation.first);
            auto newElemId = relocation.second.ElemId;
            Y_ASSERT(id2NodeMap.contains(newElemId));

            if (const auto iit = id2NodeMap.find(originalElemId)) {
                if (const auto nit = id2NodeMap.find(newElemId)) {
                    replaces.insert({iit->second, nit->second});
                }
            }
        }

        if (Y_UNLIKELY(!replaces.empty())) {
            ReplaceEdges(replaces);
        }
    }

    void Load(TBlob& multi) {
        TSubBlobs blob(multi);
        TMemoryInput input(blob[0].Data(), blob[0].Length());
        Load(&input);

        if (blob.size() == 2) {
            TMemoryInput input(blob[1].Data(), blob[1].Length());
            TSerializer<decltype(NodeData)>::Load(&input, NodeData);
        }
    }

    /// @brief save the graph
    void Save(TMultiBlobBuilder& builder) {
        // TBD/FIXME: we do not save the "changed" tag,
        // so saving a non-compacted graph then loading it back
        // basically breaks an invariant represented by that tag;
        // it seems that we should MarkUnchanged() or at least Y_ASSERT(!HasAnythingDeleted()) here,
        // but this somehow breaks expectations of some tests
        TString graphData;
        {
            TStringOutput graphOutput(graphData);
            TBase::Save(&graphOutput);
            graphOutput.Finish();
        }
        builder.AddBlob(new TBlobSaverMemory(TBlob::FromString(graphData)));
        {
            TBuffer buffer;
            TBufferOutput output(buffer);
            TSerializer<decltype(NodeData)>::Save(&output, NodeData);
            builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(buffer)));
        }
    }

    void Reset() {
        ResetId2NodeMaps();
        TBase::Reset();
    }

    static inline const TDepGraph& Graph(const TConstNodeRef& node) {
        return static_cast<const TDepGraph&>(node.Graph());
    }

    static inline TDepGraph& Graph(TNodeRef node) {
        return static_cast<TDepGraph&>(node.Graph());
    }

    static inline const TDepGraph& Graph(const TConstEdgeRef& edge) {
        return static_cast<const TDepGraph&>(edge.Graph());
    }

    static inline TDepGraph& Graph(TEdgeRef edge) {
        return static_cast<TDepGraph&>(edge.Graph());
    }

    template <bool IsConst>
    static inline const TFileView GetFileName(const TAnyNodeRef<IsConst>& node) { // slow - for rare messages & debug
        return Graph(node).GetFileName(*node);
    }

    template <bool IsConst>
    static inline const TCmdView GetCmdName(const TAnyNodeRef<IsConst>& node) { // slow - for rare messages & debug
        return Graph(node).GetCmdName(*node);
    }

    static inline TNodeRef GetInvalidNode(const TNodeRef& someNode) {
        return Graph(someNode).GetInvalidNode();
    }

    static inline TConstNodeRef GetInvalidNode(const TConstNodeRef& someNode) {
        return Graph(someNode).GetInvalidNode();
    }

    static inline TEdgeRef GetInvalidEdge(const TNodeRef& someNode) {
        return Graph(someNode).GetInvalidEdge();
    }

    static inline TConstEdgeRef GetInvalidEdge(const TConstNodeRef& someNode) {
        return Graph(someNode).GetInvalidEdge();
    }

    template <bool IsConst>
    static inline TString ToString(const TAnyNodeRef<IsConst>& node) {
        if (!node.IsValid()) {
            return "<invalid node>";
        }
        return Graph(node).ToString(*node);
    }

    template <bool IsConst>
    static inline TStringBuf ToTargetStringBuf(const TAnyNodeRef<IsConst>& node) {
        if (!node.IsValid()) {
            return "<invalid node>";
        }
        return Graph(node).ToTargetStringBuf(*node);
    }

    /// Converting to vector replacement NodeIds to ElemIds
    template<class TNodeIds>
    static TVector<ui32> NodeToElemIds(const TDepGraph& graph, const TNodeIds& nodeIds) {
        TVector<ui32> elemIds;
        elemIds.reserve(nodeIds.size());
        for (const auto nodeId : nodeIds) {
            elemIds.emplace_back(graph.Get(nodeId)->ElemId);
        }
        return elemIds;
    }

    /// @brief compact edges and nodes: node refs are invalidated
    /// This will only update edges and refs from symbols
    void Compact() {
        TBase::Compact();
        ResetId2NodeMaps();
        for (const auto& node : Nodes()) {
            SyncNameId(node);
        }
    }

    void ReportStats() const;

    using TBase::GetInvalidNode;
    using TBase::GetInvalidEdge;

private:
    TId2NodeMap& MapByType(EMakeNodeType type) {
        if (UseFileId(type)) {
            return MainId2NodeMap;
        } else {
            return CmdId2NodeMap;
        }
    }

    /// @brief set NodeId reference in symbol
    void SyncNameId(const TNodeRef& node) {
        if (node->NodeType == EMNT_Deleted || node->ElemId == 0) {
            return;
        }
        Y_ASSERT(HasId2NodeMaps());

        auto& idmap = MapByType(node->NodeType);
        idmap[node->ElemId] = node.Id();
    }

    /// @brief check that Id2Node maps are proeprly initialized
    bool HasId2NodeMaps() const {
        return !(MainId2NodeMap.empty() || CmdId2NodeMap.empty());
    }

    void ResetId2NodeMaps() {
        MainId2NodeMap.clear();
        MainId2NodeMap[0] = TNodeId::Invalid;
        CmdId2NodeMap.clear();
        CmdId2NodeMap[0] = TNodeId::Invalid;
    }

    void MakeId2NodeMaps() {
        ResetId2NodeMaps();
        for (const auto& node : Nodes()) {
            SyncNameId(node);
        }
    }

    TNodeRef GetFileNode(ui32 fid) {
        if (fid != 0) {
            return GetFileNodeById(fid);
        }
        return GetInvalidNode();
    }

    /// @brief load the graph and reconstruct node 2 nameId mappings
    /// !!! It is important to call this after names are read
    void Load(IInputStream* input) {
        TBase::Load(input);
        MakeId2NodeMaps();
    }
};

using TDepTreeDepIter = typename TDepGraph::TEdgeIterator;
using TDepNodeRef = typename TDepGraph::TNodeRef;
using TConstDepNodeRef = typename TDepGraph::TConstNodeRef;
using TDepRef = typename TDepGraph::TEdgeRef;
using TConstDepRef = typename TDepGraph::TConstEdgeRef;
