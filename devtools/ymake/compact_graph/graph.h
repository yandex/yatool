#pragma once

#include <util/generic/vector.h>
#include <util/generic/deque.h>
#include <util/generic/hash.h>
#include <util/generic/algorithm.h>
#include <util/ysaveload.h>

using TNodeId = ui32;

template <typename V>
V Deleted();

template <typename V>
bool Deleted(V);

template <typename VE, typename VN, typename TE, typename TN>
class TCompactGraph;

enum EChanged : ui8 {
    Unchanged = 0,
    EdgeAdded = 1,
    NodeAdded = 2,
    EdgeDeleted = 4,
    NodeDeleted = 8,
    HangingEdges = 16,
    EdgeChanged = 32,
};

template <typename VE, int IdBits>
class TCompactEdge {
private:
    TNodeId ToAndValue_;
    static constexpr TNodeId NodeMask = ~((TNodeId)-1 << IdBits);
    static constexpr TNodeId Deleted = 0;

public:
    using TValueType = VE;

    TCompactEdge()
        : ToAndValue_(Deleted)
    {
    }

    TCompactEdge(TNodeId to, VE value)
        : ToAndValue_((value << IdBits) | to)
    {
        Y_ASSERT(value < ((TNodeId)1 << (sizeof(TNodeId) * 8 - IdBits)));
        Y_ASSERT(to < ((TNodeId)1 << IdBits));
    }

    const VE Value() const {
        return static_cast<VE>(ToAndValue_ >> IdBits);
    }

    void SetValue(VE newValue) {
        Y_ASSERT(newValue < ((TNodeId)1 << (sizeof(TNodeId) * 8 - IdBits)));
        ToAndValue_ = Id() | (newValue << IdBits);
    }

    TNodeId Id() const {
        return ToAndValue_ & NodeMask;
    }

    void SetId(TNodeId to) {
        ToAndValue_ = (Value() << IdBits) | to;
    }

    bool IsValid() const {
        return Id() != 0;
    }

    void Delete() {
        ToAndValue_ = Deleted;
    }

    /// Compact internal representation of compact edge (to and value), can be used as id of compact edge
    TNodeId Representation() const noexcept {
        return ToAndValue_;
    }

    Y_SAVELOAD_DEFINE(ToAndValue_);
};

template <typename VE>
class TEdge {
private:
    TNodeId To_;
    VE Value_;

    static constexpr TNodeId Deleted = 0;

public:
    using TValueType = VE;

    TEdge()
        : To_(Deleted)
        , Value_(VE())
    {
    }

    TEdge(TNodeId to, VE value)
        : To_(to)
        , Value_(value)
    {
    }

    const VE Value() const {
        return Value_;
    }

    void SetValue(VE newValue) {
        Value_ = newValue;
    }

    TNodeId Id() const {
        return To_;
    }

    void SetId(TNodeId to) {
        To_ = to;
    }

    bool IsValid() const {
        return Id() != Deleted;
    }

    void Delete() {
        To_ = Deleted;
    }

    Y_SAVELOAD_DEFINE(To_, Value_);
};

template <typename VN, typename TE, typename C = TVector<TE>>
class TNode: public TMoveOnly {
private:
    using TEdgeValue = typename TE::TValueType;
    VN Value_;
    C Edges_;

public:
    using TEdges = C;
    using TValueType = VN;

    TNode()
        : Value_(Deleted<VN>())
    {
    }

    explicit TNode(VN value)
        : Value_(value)
    {
    }

    const VN& Value() const {
        return Value_;
    }

    VN& Value() {
        return Value_;
    }

    void SetValue(const VN& newValue) {
        Value_ = newValue;
    }

    bool IsValid() const {
        return !Deleted(Value());
    }

    void Delete() {
        SetValue(Deleted<VN>());
    }

    C& Edges() {
        return Edges_;
    }

    const C& Edges() const {
        return Edges_;
    }

    TE& AddEdge(TNodeId to, TEdgeValue value) {
        Edges_.emplace_back(to, value);
        return Edges_.back();
    }

    template<typename Comp>
    void SortEdges(Comp&& comp) {
        StableSort(Edges_.begin(), Edges_.end(), comp);
    }

    void ClearEdges() {
        for (auto&& edge : Edges_) {
            edge.Delete();
        }
    }

    Y_SAVELOAD_DEFINE(Value_, Edges_);
};

/// @brief helper type to construct Const and NonConst references for a class
template <typename Type, bool IsConst>
using TAddRef = typename std::conditional<IsConst, Type const&, Type&>::type;

template <typename Type, bool IsConst>
using TAddPtr = typename std::conditional<IsConst, const Type*, Type*>::type;

template <typename VE, typename VN, typename TE, typename TN>
class TCompactGraph;

/// @brief The reference to edge with controlled constness
///        Const version doesn't allow modifications to itself and to its graph
template <typename VE, typename VN, typename TE, typename TN, bool IsConst>
class TEdgeRefBase {
private:
    using TGraph = TCompactGraph<VE, VN, TE, TN>;
    using TGraphRef = TAddRef<TGraph, IsConst>;

    using TConstNodeRef = typename TGraph::TConstNodeRef;
    using TNodeRef = typename TGraph::template TAnyNodeRef<IsConst>;
    using TMutableSelf = TEdgeRefBase<VE, VN, TE, TN, false>;
    using TConstSelf = TEdgeRefBase<VE, VN, TE, TN, true>;

    using TEdgesRef = TAddRef<typename TN::TEdges, IsConst>;
    using TERef = TAddRef<TE, IsConst>;

    TGraphRef Graph_;
    TEdgesRef Edges_;
    size_t Idx_;
    TNodeId From_;

    friend TConstSelf;

private:
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    TERef Edge() {
        return Edges_[Idx_];
    }
    const TE& Edge() const {
        return Edges_[Idx_];
    }

public:
    TEdgeRefBase(size_t idx, TEdgesRef edges, TNodeId from, TGraphRef graph)
        : Graph_(graph)
        , Edges_(edges)
        , Idx_(idx)
        , From_(from)
    {
    }

    TEdgeRefBase(const TEdgeRefBase& nonConstRef) = default;

    /// Allow conversion of non-const to const
    template <bool C = IsConst,
              typename TOther = typename std::enable_if<C, TMutableSelf>::type,
              typename = typename std::enable_if<std::is_same<TOther, TMutableSelf>::value>::type>
    TEdgeRefBase(const TOther& nonConstRef)
        : Graph_(nonConstRef.Graph_)
        , Edges_(nonConstRef.Edges_)
        , Idx_(nonConstRef.Idx_)
        , From_(nonConstRef.From_)
    {
    }

    VE Value() const {
        return Edge().Value();
    }

    VE operator*() const {
        return Value();
    }

    bool operator==(const TEdgeRefBase& other) const {
        // Edges are same if they have same value, connect same nodes and belong to the exactly same graph
        return &Graph() == &other.Graph() && Value() == other.Value() && From() == other.From() && To() == other.To();
    }

    bool operator!=(const TEdgeRefBase& other) const {
        return !(*this == other);
    }

    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    void SetValue(VE newValue) {
        Y_ASSERT(IsValid());
        Edge().SetValue(newValue);
    }

    /// @brief check edge validity
    /// This function is hot, so we try to use as few checks as possible
    /// we assume that graph change status is cached, so access to it is faster
    /// than other, more scattered accesses, traversing to To() and From() nodes being slowest
    bool IsValid() const {
        Y_ASSERT(Idx_ <= Edges_.size());
        if (Idx_ == Edges_.size()) {
            return false;
        }
        ui8 changed = Graph().GetChanged();
        if (Y_LIKELY((changed & (EChanged::EdgeDeleted | EChanged::NodeDeleted)) == 0)) {
            // There should be no invalid edges if no deletion happened. This is the most frequent case
            Y_ASSERT(Edge().IsValid() && To().IsValid() && From().IsValid());
            return true;
        } else if (Y_LIKELY((changed & EChanged::HangingEdges) == 0)) {
            Y_ASSERT(!Edge().IsValid() || To().IsValid() && From().IsValid());
            // If no hanging edges in graph it is enough to check the edge state itslef.
            // This should be next frequent case: hanging edges are result of nodes deletion those are rare
            // It is advised to use DeleteHangingEdges() and/or AssertNoHangingEdges() to hit this branch
            return Edge().IsValid();
        } else {
            // This branch is slowest: it requires indirection by graph to ensure nodes validity
            return Edge().IsValid() && To().IsValid() && From().IsValid();
        }
    }

    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    void Delete() {
        Edge().Delete();
        Graph_.NotifyChanged(EChanged::EdgeDeleted);
    }

    TNodeRef From() const {
        Y_ASSERT(Edge().IsValid());
        return Graph_[From_];
    }

    TNodeRef To() const {
        Y_ASSERT(Edge().IsValid());
        return Graph_[Edge().Id()];
    }

    /// @brief returns index of the edge in edges collection which includes deleted ones
    size_t Index() const {
        return Idx_;
    }

    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    TGraphRef Graph() {
        return Graph_;
    }

    const TGraph& Graph() const {
        return Graph_;
    }
};

/// @brief iterator over edges with controlled constness of returned references
template <typename VE, typename VN, typename TE, typename TN, bool IsConst>
class TEdgeIterator {
private:
    using TGraph = TCompactGraph<VE, VN, TE, TN>;
    using TGraphRef = TAddRef<TGraph, IsConst>;
    using TEdgesRef = TAddRef<typename TN::TEdges, IsConst>;
    using TNodeRef = typename TGraph::template TAnyNodeRef<IsConst>;

public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = VE;
    using difference_type = ptrdiff_t;
    using pointer = value_type*;
    using reference = typename TGraph::template TAnyEdgeRef<IsConst>;

private:
    size_t Cur_;
    TAddPtr<typename TN::TEdges, IsConst> Edges_;
    TAddPtr<TGraph, IsConst> Graph_;
    TNodeId From_;

    /// @brief check edge validity
    /// This function is hot, so we try to use as few checks as possible
    /// See TEdgeRef::IsValid() for more comments
    bool IsValid() const {
        Y_ASSERT(Cur_ <= Edges_->size());
        if (Cur_ == Edges_->size()) {
            return true;
        }

        ui8 changed = Graph_->GetChanged();
        if (Y_LIKELY((changed & (EChanged::EdgeDeleted | EChanged::NodeDeleted)) == 0)) {
            Y_ASSERT(((*Edges_)[Cur_].IsValid() && (*Graph_)[(*Edges_)[Cur_].Id()].IsValid()));
            return true;
        } else if (Y_LIKELY((changed & EChanged::HangingEdges) == 0)) {
            Y_ASSERT(!(*Edges_)[Cur_].IsValid() || (*Graph_)[(*Edges_)[Cur_].Id()].IsValid());
            return (*Edges_)[Cur_].IsValid();
        } else {
            return (*Edges_)[Cur_].IsValid() && (*Graph_)[(*Edges_)[Cur_].Id()].IsValid();
        }
    }

    void Advance() {
        Y_ASSERT((*Graph_)[From_].IsValid());
        do {
            ++Cur_;
        } while (!IsValid());
    }

public:
    TEdgeIterator(TGraphRef graph, TNodeId from)
        : TEdgeIterator(graph[from].Edges().begin())
    {
    }

    TEdgeIterator(size_t cur, TEdgesRef edges, TGraphRef graph, TNodeId from)
        : Cur_(cur)
        , Edges_(&edges)
        , Graph_(&graph)
        , From_(from)
    {
        if (!IsValid()) {
            Advance();
        }
    }

    bool operator==(const TEdgeIterator& other) const {
        return Cur_ == other.Cur_;
    }

    bool operator!=(const TEdgeIterator& other) const {
        return !(Cur_ == other.Cur_);
    }

    TEdgeIterator& operator++() {
        Advance();
        return *this;
    }

    TEdgeIterator operator++(int) {
        TEdgeIterator result = *this;
        Advance();
        return result;
    }

    reference operator*() const {
        return reference(Cur_, *Edges_, From_, *Graph_);
    }

    TNodeRef Node() const {
        return (*Graph_)[From_];
    }

    /// @brief since we know our end position we may implement internal check
    bool AtEnd() const {
        return Cur_ == Edges_->size();
    }

    /// @brief returns index in edges collection which includes deleted ones
    size_t Index() const {
        return Cur_;
    }

    /// @brief returns number of edges including deleted ones
    size_t Total() const {
        return Edges_->size();
    }
};

/// @brief The reference to node with controlled constness
///        Const version doesn't allow modifications to itself and to its graph
template <typename VE, typename VN, typename TE, typename TN, bool IsConst>
class TNodeRefBase {
public:
    using TGraph = TCompactGraph<VE, VN, TE, TN>;
    using TIterator = typename TGraph::template TAnyEdgeIterator<IsConst>;
    using TConstIterator = typename TGraph::TConstEdgeIterator;
    using TEdgeRef = typename TGraph::template TAnyEdgeRef<IsConst>;
    using TConstEdgeRef = typename TGraph::TConstEdgeRef;

private:
    using TConstSelf = TNodeRefBase<VE, VN, TE, TN, true>;
    using TMutableSelf = TNodeRefBase<VE, VN, TE, TN, false>;
    using TGraphRef = TAddRef<TGraph, IsConst>;
    using TNRef = TAddRef<TN, IsConst>;

    // Let graph perform low-level operations over underlying Node
    friend TGraph;
    friend TConstSelf;

    TGraphRef Graph_;
    TNodeId Id_;
    TNRef Node_;

    /// @brief provide iteration over edges with controlled constness
    template <bool IsConstView>
    class TEdgesView {
    private:
        using TEdgesRef = TAddRef<typename TN::TEdges, IsConstView>;
        using TGraphRef = TAddRef<TGraph, IsConstView>;
        using TIterator = typename TGraph::template TAnyEdgeIterator<IsConstView>;

        TGraphRef Graph_;
        TEdgesRef Edges_;
        TNodeId From_;

    public:
        TEdgesView(TNodeId from, TEdgesRef edges, TGraphRef graph)
            : Graph_(graph)
            , Edges_(edges)
            , From_(from)
        {
        }

        TIterator begin() const {
            if (!Graph_[From_].IsValid()) {
                return end();
            }
            return TIterator(0, Edges_, Graph_, From_);
        }

        TIterator end() const {
            return TIterator(Edges_.size(), Edges_, Graph_, From_);
        }

        bool IsEmpty() const {
            return begin() == end();
        }

        /// @brief returns number of edges including deleted ones
        size_t Total() const {
            return Edges_.size();
        }
    };

public:
    TNodeRefBase(TNRef Node, TNodeId Id, TGraphRef Graph)
        : Graph_(Graph)
        , Id_(Id)
        , Node_(Node)
    {
    }

    TNodeRefBase(const TNodeRefBase& nonConstRef) = default;

    /// @brief allow construction of ConstRef from MutableRef
    /// Note: we need 2nd enable_if to avoid too broad matching of TOther
    template <bool C = IsConst,
              typename TOther = typename std::enable_if<C, TMutableSelf>::type,
              typename = typename std::enable_if<std::is_same<TOther, TMutableSelf>::value>::type>
    TNodeRefBase(const TOther& nonConstRef)
        : Graph_(nonConstRef.Graph_)
        , Id_(nonConstRef.Id_)
        , Node_(nonConstRef.Node_)
    {
    }

    /// @brief returns Id of a node within originating graph
    /// Id is just an index of a node in graph's storage
    TNodeId Id() const {
        return Id_;
    }

    /// @brief read-only access to node payload
    const VN& Value() const {
        Y_ASSERT(IsValid());
        return Node_.Value();
    }

    /// @brief read-only access to node payload
    const VN& operator*() const {
        return Value();
    }

    /// @brief read-only access to payload members
    const VN* operator->() const {
        return &Value();
    }

    /// @brief Edges are same if belong to the exactly same graph and have same Id (index within the graph)
    bool operator==(const TNodeRefBase& other) const {
        return &Graph() == &other.Graph() && Id() == other.Id();
    }

    /// @brief Edges are same if belong to the exactly same graph and have same Id (index within the graph)
    bool operator!=(const TNodeRefBase& other) const {
        return !(*this == other);
    }

    /// @brief Node is not deleted (Node with id 0 always present in deleted/invalid state)
    bool IsValid() const {
        return Node_.IsValid();
    }

    /// @brief Provide access to iteration over edges. Preserve constness of Ref
    TEdgesView<IsConst> Edges() const {
        return TEdgesView<IsConst>(Id(), Node_.Edges(), Graph_);
    }

    /// @brief Provide constant access to iteration over edges
    TEdgesView<true> ConstEdges() const {
        return TEdgesView<true>(Id(), Node_.Edges(), Graph_);
    }

    /// @brief Provide constant access to underlying graph
    const TGraph& Graph() const {
        return Graph_;
    }

    /// @brief mutable access to value from non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    VN& Value() {
        Y_ASSERT(IsValid());
        return Node_.Value();
    }

    /// @brief mutable access to value from non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    VN& operator*() {
        return Value();
    }

    /// @brief access to mutating members from non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    VN* operator->() {
        return &Value();
    }

    /// @brief add unique edge from this node to given one.
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    std::pair<TEdgeRef, bool> AddUniqueEdge(TConstSelf to, VE value) {
        Y_ASSERT(IsValid());
        Y_ASSERT(to.IsValid());
        return AddUniqueEdge(to.Id(), value);
    }

    /// @brief add edge from this node to given one.
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    TEdgeRef AddEdge(TConstSelf to, VE value) {
        Y_ASSERT(IsValid());
        Y_ASSERT(to.IsValid());
        return AddEdge(to.Id(), value);
    }

    /// @brief add unique edge from this node to given one.
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    std::pair<TEdgeRef, bool> AddUniqueEdge(TNodeId to, VE value) {
        Y_ASSERT(IsValid());
        Y_ASSERT(Graph_[to].IsValid());

        const auto edgeIt = FindIf(Edges(), [=](const auto& dep) { return dep.To().Id() == to && dep.Value() == value; });
        if (edgeIt == Edges().end()) {
            return std::make_pair(AddEdge(to, value), true);
        } else {
            return std::make_pair(*edgeIt, false);
        }
    }

    /// @brief add edge from this node to given one.
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    TEdgeRef AddEdge(TNodeId to, VE value) {
        Y_ASSERT(IsValid());
        Y_ASSERT(Graph_[to].IsValid());

        Graph_.NotifyChanged(EChanged::EdgeAdded);
        Node_.AddEdge(to, value);
        return TEdgeRef(Node_.Edges().size() - 1, Node_.Edges(), Id(), Graph_);
    }

    /// @brief clear edges list
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    void ClearEdges() {
        Y_ASSERT(IsValid());
        Graph_.NotifyChanged(EChanged::EdgeDeleted);
        Node_.ClearEdges();
    }

    /// @brief sort edges stably with given comparator
    /// Only available in non-const Ref
    template <typename Comp, bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    void SortEdges(Comp&& comp) {
        Y_ASSERT(IsValid());
        Graph_.NotifyChanged(EChanged::EdgeAdded); // TODO: set valid value
        Node_.SortEdges(comp);
    }

    /// @brief Set value of payload
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    void SetValue(VN newValue) {
        Y_ASSERT(IsValid());
        Node_.SetValue(newValue);
    }

    /// @brief Delete node (make it invalid)
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    void Delete() {
        Graph_.DeleteNode(Id());
    }

    /// @brief Retrieve non-const reference to underlying graph for modifications
    /// Only available in non-const Ref
    template <bool C = !IsConst, typename = typename std::enable_if<C, int>::type>
    TGraphRef Graph() {
        return Graph_;
    }
};

/// @brief iterator over nodes with controlled constness of returned references
template <typename VE, typename VN, typename TE, typename TN, bool IsConst>
class TNodeIterator {
private:
    using TGraph = TCompactGraph<VE, VN, TE, TN>;
    using TRef = typename TGraph::template TAnyNodeRef<IsConst>;
    using TGraphRef = TAddRef<TGraph, IsConst>;
    using TNRef = TAddRef<TN, IsConst>;

    TGraphRef Graph_;
    TNodeId Id_;

private:
    bool IsValid() const {
        return Id_ == Graph_.Nodes_.size() || Cur().IsValid();
    }

    void Advance() {
        do {
            ++Id_;
        } while (!IsValid());
    }

public:
    TNodeIterator(TNodeId id, TGraphRef graph)
        : Graph_(graph)
        , Id_(id)
    {
        if (!IsValid()) {
            Advance();
        }
    }

    bool operator==(const TNodeIterator<VE, VN, TE, TN, IsConst>& other) const {
        return Id_ == other.Id_ && &Graph_ == &other.Graph_;
    }

    bool operator!=(const TNodeIterator<VE, VN, TE, TN, IsConst>& other) const {
        return !(*this == other);
    }

    TNodeIterator& operator++() {
        Y_ASSERT(Id_ < Graph_.Nodes_.size());
        Advance();
        return *this;
    }

    TRef operator*() const {
        return TRef(Cur(), Id_, Graph_);
    }

private:
    TNRef Cur() {
        return Graph_.Nodes_[Id_];
    }
    const TNRef Cur() const {
        return Graph_.Nodes_[Id_];
    }
};

template <typename VE, typename VN, typename TE = TEdge<VE>, typename TN = TNode<VN, TE>>
class TCompactGraph: private TNonCopyable {
public:
    template <bool IsConst>
    using TAnyNodeRef = TNodeRefBase<VE, VN, TE, TN, IsConst>;
    template <bool IsConst>
    using TAnyEdgeRef = TEdgeRefBase<VE, VN, TE, TN, IsConst>;
    template <bool IsConst>
    using TAnyIterator = TNodeIterator<VE, VN, TE, TN, IsConst>;
    template <bool IsConst>
    using TAnyEdgeIterator = ::TEdgeIterator<VE, VN, TE, TN, IsConst>;

    using TEdge = TE;

    using TNodeRef = TAnyNodeRef<false>;
    using TEdgeRef = TAnyEdgeRef<false>;
    using TIterator = TAnyIterator<false>;
    using TEdgeIterator = TAnyEdgeIterator<false>;

    using TConstNodeRef = TAnyNodeRef<true>;
    using TConstEdgeRef = TAnyEdgeRef<true>;
    using TConstIterator = TAnyIterator<true>;
    using TConstEdgeIterator = TAnyEdgeIterator<true>;

private:
    using TNodes = TDeque<TN>;

    TNodes Nodes_;
    ui8 Changed_;

    /// @brief provide iteration over nodes with controlled constness
    template <bool IsConstView>
    class TNodesView {
    private:
        using TIterator = TNodeIterator<VE, VN, TE, TN, IsConstView>;
        using TGraphRef = TAddRef<TCompactGraph, IsConstView>;
        TGraphRef Graph_;

    public:
        explicit TNodesView(TGraphRef graph)
            : Graph_(graph)
        {
        }

        TIterator begin() const {
            return TIterator(1, Graph_);
        }

        TIterator end() const {
            return TIterator(Graph_.Nodes_.size(), Graph_);
        }

        bool IsEmpty() const {
            return begin() == end();
        }
    };

    // Provide access to internal collection for iteration
    friend TIterator;
    friend TConstIterator;

public:
    explicit TCompactGraph()
        : Changed_(EChanged::Unchanged)
    {
        Reset();
    }

    /// @brief Get modifiable reference to node
    /// Note that deleted nodes are accessible: check IsValid() after obtaining node reference
    TNodeRef Get(TNodeId id) {
        Y_ASSERT(id < Size());
        if (Y_LIKELY(id < Size())) {
            return TNodeRef(Nodes_[id], id, *this);
        } else {
            return GetInvalidNode();
        }
    }

    /// @brief Get read-only reference to node
    /// Note that deleted nodes are accessible: check IsValid() after obtaining node reference
    TConstNodeRef Get(TNodeId id) const {
        Y_ASSERT(id < Size());
        if (Y_LIKELY(id < Size())) {
            return TConstNodeRef(Nodes_[id], id, *this);
        } else {
            return GetInvalidNode();
        }
    }

    /// @brief Get validated mutable reference to node
    /// For invalid and out of bounds nodes exception will be thrown
    TNodeRef GetValid(TNodeId id) {
        if (id >= Size() || !Nodes_[id].Valid()) {
            ythrow yexception() << "Validated mutable access to graph node #" << id << " failed: " << id >= Size() ? "Out of bounds" : "Deleted node";
        }
        return Get(id);
    }

    /// @brief Get validated read-only reference to node
    /// For invalid and out of bounds nodes exception will be thrown
    TConstNodeRef GetValid(TNodeId id) const {
        if (id >= Size() || !Nodes_[id].Valid()) {
            ythrow yexception() << "Validated const access to graph node #" << id << " failed: " << id >= Size() ? "Out of bounds" : "Deleted node";
        }
        return Get(id);
    }

    TNodeRef operator[](TNodeId id) {
        return Get(id);
    }

    TConstNodeRef operator[](TNodeId id) const {
        return Get(id);
    }

    TNodeRef AddNode(VN value) {
        Nodes_.emplace_back(value);
        NotifyChanged(EChanged::NodeAdded);
        return TNodeRef(Nodes_.back(), Nodes_.size() - 1, *this);
    }

    void DeleteNode(TConstNodeRef node) {
        return DeleteNode(node.Id());
    }

    void DeleteNode(TNodeId id) {
        Y_ASSERT(id < Size());
        if (Nodes_[id].IsValid()) {
            // Avoid state change if node was already deleted
            Nodes_[id].Delete();
            NotifyChanged(EChanged::NodeDeleted);
            NotifyChanged(EChanged::HangingEdges);
        }
    }

    TNodesView<false> Nodes() {
        return TNodesView<false>(*this);
    }

    TNodesView<true> Nodes() const {
        return TNodesView<true>(*this);
    }

    TNodesView<true> ConstNodes() const {
        return TNodesView<true>(*this);
    }

    size_t Size() const {
        Y_ASSERT(Nodes_.size() > 0);
        return Nodes_.size();
    }

    /// @brief add edge between 2 existing nodes
    TEdgeRef AddEdge(TConstNodeRef from, TNodeId to, VE value) {
        return AddEdge(from.Id(), to, value);
    }

    /// @brief add edge between 2 existing nodes
    TEdgeRef AddEdge(TNodeId from, TNodeId to, VE value) {
        Y_ASSERT(from < Size() && to < Size() && Nodes_[from].IsValid() && Nodes_[to].IsValid());
        return (*this)[from].AddEdge(to, value);
    }

    /// @brief add edge between 2 nodes with nodes validation
    TEdgeRef AddEdgeValid(TNodeId from, TNodeId to, VE value) {
        return GetValid(from).AddEdge(GetValid(to).Id(), value);
    }

    ui8 GetChanged() const {
        return Changed_;
    }

    bool IsChanged() const {
        return Changed_ != EChanged::Unchanged;
    }

    /// @brief notify graph about changes in nodes or edges set
    void NotifyChanged(EChanged change) {
        Changed_ |= change;
    }

    /// @brief Mark graph as unchanged
    /// This allows monitor changes from certain moment
    /// Since compaction monitors deletion it is prohibited to make graph
    /// unchanged in non-compacted state
    void MarkUnchanged() {
        Y_ASSERT(!HasAnythingDeleted());
        Changed_ = EChanged::Unchanged;
    }

    /// @brief Check that graph contains deleted node or edge
    bool HasAnythingDeleted() const {
        bool nres = Changed_ & (EChanged::EdgeDeleted | EChanged::NodeDeleted);
        Y_ASSERT(HasAnythingDeletedSlow() == nres);
        return nres;
    }

    /// @brief check whether there may be hanging edge in the graph
    bool MayHaveHangingEdges() const {
        Y_ASSERT(!HasHangingEdgesSlow() || (Changed_ & EChanged::HangingEdges));
        return Changed_ & EChanged::HangingEdges;
    }

    /// @brief Records guarantee that node removal doesn't leave any hanging edges
    void AssertNoHangingEdges() {
        if (MayHaveHangingEdges()) {
            Changed_ &= ~EChanged::HangingEdges;
        }
    }

    /// @brief mark all edges leading from/to deleted nodes as deleted
    /// This process doesn't invalidate anything, it just
    /// accelerates Edge.IsValid() operations
    void DeleteHangingEdges() {
        for (auto& node : Nodes_) {
            DeleteHangingEdgesImpl(node);
        }
        Changed_ &= ~EChanged::HangingEdges;
    }

    /// @brief mark all edges leading to deleted nodes as deleted
    ///        if node is deleted - mark all its edges as deleted
    void DeleteHangingEdges(TNodeRef node) {
        DeleteHangingEdgesImpl(node.Node_);
    }

    /// @brief mark all edges leading to deleted nodes as deleted
    ///        if node is deleted - mark all its edges as deleted
    void DeleteHangingEdges(TNodeId node) {
        DeleteHangingEdgesImpl(Nodes_[node]);
    }

    /// @brief check whether there is hanging edge in graph.
    /// Slow! Use for debug/testing purposes only
    bool HasHangingEdges() const {
        bool has = HasHangingEdgesSlow();
        Y_ASSERT(!has || (Changed_ & EChanged::HangingEdges));
        return has;
    }

    /// @brief change ends of a specific node's edges according to a map
    void ReplaceEdges(TNodeId nodeId, const THashMap<TNodeId, TNodeId>& replaces) {
        Y_ASSERT(nodeId < Size() && Nodes_[nodeId].IsValid());
        ReplaceEdges(Nodes_[nodeId], replaces);
    }

    /// @brief change ends of all edges according to a map
    /// Hanging nodes will be marked as deleted
    void ReplaceEdges(const THashMap<TNodeId, TNodeId>& replaces) {
        for (auto&& node: Nodes_) {
            if (node.IsValid()) {
                ReplaceEdges(node, replaces);
            }
        }
        for (const auto& replace : replaces) {
            Nodes_[replace.first].Delete();
        }
    }

    /// @brief change ends of a specific node's edges according to a map
    /// Can replace single edge with zero or multiple new edges.
    void ReplaceEdgesWithList(TNodeId nodeId, const THashMap<TNodeId, TVector<TNodeId>>& replaces) {
        Y_ASSERT(nodeId < Size() && Nodes_[nodeId].IsValid());
        TN& node = Nodes_[nodeId];
        const size_t edgeCnt = node.Edges().size();
        for (size_t j = 0; j < edgeCnt; ++j) {
            auto& edge = node.Edges()[j];
            const auto replaceIt = replaces.find(edge.Id());
            if (replaceIt != replaces.end()) {
                const auto& newDest = replaceIt->second;
                if (newDest.empty()) {
                    edge.Delete();
                    NotifyChanged(EChanged::EdgeDeleted);
                    continue;
                }
                TNodeId newEnd = newDest[0];
                Y_ASSERT(newEnd < Size() && Nodes_[newEnd].IsValid());
                edge.SetId(newEnd);
                for (size_t i = 1; i < newDest.size(); i++) {
                    newEnd = newDest[i];
                    Y_ASSERT(newEnd < Size() && Nodes_[newEnd].IsValid());
                    node.AddEdge(newEnd, edge.Value());
                }
            }
        }
        NotifyChanged(EChanged::EdgeChanged);
    }

    Y_SAVELOAD_DEFINE(Nodes_);

protected:
    /// @brief return knowingly invalid node
    TNodeRef GetInvalidNode() {
        return Get(0);
    }

    /// @brief return knowingly invalid node
    TConstNodeRef GetInvalidNode() const {
        return Get(0);
    }

    /// @brief return knowingly invalid edge
    TEdgeRef GetInvalidEdge() {
        return TEdgeRef(0, Nodes_[0].Edges(), 0, *this);
    }

    /// @brief return knowingly invalid edge
    TConstEdgeRef GetInvalidEdge() const {
        return TConstEdgeRef(0, Nodes_[0].Edges(), 0, *this);
    }

    /// @brief compact edges in node
    /// All references to nodes remain intact and we assume
    /// no long-living references to edges
    void CompactEdges(TNodeRef& node) {
        CompactEdgesImpl(node.Node_);
    }

    /// @brief Reset graph into empty state
    void Reset() {
        Nodes_.clear();
        Nodes_.emplace_back();
        Changed_ = EChanged::Unchanged;
    }

    /// @brief traverse through entire graph and find first matching edge
    template <typename NodePred, typename EdgePred>
    inline bool FindIf(NodePred nodePred, EdgePred edgePred) const;

    /// @brief compact edges in entire graph
    /// All references to nodes remain intact and we assume
    /// no long-living references to edges
    inline void CompactEdges();

    /// @brief Compact graph by removing deleted nodes and edges
    /// IDs in remaining edges are updated according to
    /// changes in nodes
    inline void Compact();

private:
    /// @brief change ends of a specific node's edges according to a map
    void ReplaceEdges(TN& node, const THashMap<TNodeId, TNodeId>& replaces) {
        for (auto& edge : node.Edges()) {
            const auto replaceIt = replaces.find(edge.Id());
            if (replaceIt != replaces.end()) {
                const TNodeId newEdgeEnd = replaceIt->second;
                Y_ASSERT(newEdgeEnd < Size() && Nodes_[newEdgeEnd].IsValid());
                edge.SetId(newEdgeEnd);
            }
        }
        NotifyChanged(EChanged::EdgeChanged);
    }

    /// @brief remove invalid edges from node and compress the edges array
    void CompactEdgesImpl(TN& node) {
        if (!node.IsValid()) {
            node.Edges().clear();
            return;
        }
        EraseIf(node.Edges(), [](const TE& edge) { return !edge.IsValid(); });
    }

    /// @brief mark all edges from the node to deleted nodes as deleted
    ///        if node itself is deleted mark all its edges as deleted
    void DeleteHangingEdgesImpl(TN& node) {
        for (auto& edge : node.Edges()) {
            if (edge.IsValid() && (!node.IsValid() || !Nodes_[edge.Id()].IsValid())) {
                edge.Delete();
            }
        }
    }

    /// @brief tries to locate hanging edge (the one not marked deleted, having one of its ends deleted)
    bool HasHangingEdgesSlow() const {
        return FindIf([](const TN&) { return false; }, [this](const TN& node, const TE& edge) { return edge.IsValid() && (!node.IsValid() || !Nodes_[edge.Id()].IsValid()); });
    }

    /// @brief tries to located deleted node or edge.
    /// Slow! Used for debug validation purposes only
    bool HasAnythingDeletedSlow() const {
        return FindIf([](const TN& node) { return !node.IsValid(); },
                      [](const TN&, const TE& edge) { return !edge.IsValid(); });
    }
};

template <typename VE, typename VN, typename TE, typename TN>
template <typename NodePred, typename EdgePred>
bool TCompactGraph<VE, VN, TE, TN>::FindIf(NodePred nodePred, EdgePred edgePred) const {
    bool first = true;
    for (auto& node : Nodes_) {
        if (first) { // Skip first node
            first = false;
            continue;
        }
        if (nodePred(node)) {
            return true;
        }
        for (auto& edge : node.Edges()) {
            if (edgePred(node, edge)) {
                return true;
            }
        }
    }
    return false;
}

template <typename VE, typename VN, typename TE, typename TN>
void TCompactGraph<VE, VN, TE, TN>::CompactEdges() {
    if ((Changed_ & EChanged::EdgeDeleted) == 0) {
        return;
    }
    for (auto& node : Nodes_) {
        CompactEdgesImpl(node);
    }
    // Only edges changed: update state
    Changed_ &= ~EChanged::EdgeDeleted;
}

template <typename VE, typename VN, typename TE, typename TN>
void TCompactGraph<VE, VN, TE, TN>::Compact() {
    if (Changed_ & (EChanged::EdgeDeleted | EChanged::NodeDeleted) == 0) {
        return;
    }

    if ((Changed_ & EChanged::NodeDeleted) == 0) {
        Y_ASSERT((Changed_ & EChanged::HangingEdges) == 0);
        CompactEdges();
        return;
    }

    TVector<TNodeId> moves(Nodes_.size(), 0);
    TNodeId curOld = 0, curNew = 0;

    // Collect updated indices for nodes
    // using same logic as in remove
    for (auto& node : Nodes_) {
        if (curOld == 0 || node.IsValid()) {
            moves[curOld] = curNew;
            ++curNew;
        }
        ++curOld;
    }

    // Erase all invalid nodes except first one
    Nodes_.erase(std::remove_if(Nodes_.begin() + 1, Nodes_.end(),
                                [](auto& node) { return !node.IsValid(); }),
                 Nodes_.end());

    // Update all Ids in edges, remove reference to deleted nodes
    for (auto& node : Nodes_) {
        for (auto& edge : node.Edges()) {
            TNodeId newId = moves[edge.Id()];
            if (newId != 0) {
                edge = TE(newId, edge.Value());
            } else {
                edge.Delete();
            }
        }
        CompactEdgesImpl(node);
    }
    Changed_ &= ~(EChanged::EdgeDeleted | EChanged::NodeDeleted | EChanged::HangingEdges);
}
