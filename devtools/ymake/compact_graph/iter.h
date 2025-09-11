#pragma once

#include "dep_graph.h"
#include "dep_types.h"
#include "query.h"

#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/dbg.h>

#include <util/generic/vector.h>
#include <util/generic/hash.h>

/// @brief Base state item for depth-first traversal and, maybe, some other future algorithms
template <bool IsConst_ = false, typename TIteratedGraph = TDepGraph>
class TGraphIteratorStateItemBase {
public:
    static constexpr bool IsConst = IsConst_;
    using TGraph = TIteratedGraph;
    using TNodeRef = typename TGraph::template TAnyNodeRef<IsConst>;
    using TDepRef = typename TGraph::template TAnyEdgeRef<IsConst>;
    using TConstNodeRef = typename TGraph::TConstNodeRef;
    using TConstDepRef = typename TGraph::TConstEdgeRef;
    using TDepIter = typename TGraph::template TAnyEdgeIterator<IsConst>;

    TDepIter DepIter; // Contains reference to the node it belongs
    void* Cookie;     // usually for global data like TEntryStats
    bool IsStart;
    bool AcceptedDep;

    /// @brief create state item for Node
    /// Usually state stack starts from the Node, this c'tor allows creation of
    /// such initial states.
    explicit TGraphIteratorStateItemBase(const TNodeRef& node)
        : DepIter(node.Edges().begin())
        , Cookie(nullptr)
        , IsStart(true)
        , AcceptedDep(false)
    {
    }

    /// @brief create state item for Edge
    /// During DF traversal visited edges define state, so this c'tor is used during
    /// in-depth traversal and by default assumed as non-start.
    explicit TGraphIteratorStateItemBase(const TDepRef& dep)
        : DepIter(dep.To().Edges().begin())
        , Cookie(nullptr)
        , IsStart(false)
        , AcceptedDep(false)
    {
    }

    /// @brief access to source node
    const TNodeRef Node() const {
        return DepIter.Node();
    }

    /// @brief access to source node
    TNodeRef Node() {
        return DepIter.Node();
    }

    /// @brief Return current dependency edge
    TDepRef CurDep() {
        Y_ASSERT(!AtEnd());
        return *DepIter;
    }

    /// @brief Return current dependency edge
    const TDepRef CurDep() const {
        Y_ASSERT(!AtEnd());
        return *DepIter;
    }

    /// @brief Advance edges iterator
    TDepIter& AdvanceDep() {
        Y_ASSERT(!AtEnd());
        ++DepIter;
        AcceptedDep = false;
        return DepIter;
    }

    /// @brief Check whether edges iterator reached end of the list
    TDepIter EndDep() const {
        return Node().Edges().end();
    }

    /// @brief Check whether edges iterator reached end of the list
    bool AtEnd() const {
        return DepIter.AtEnd();
    }

    /// @brief returns index of the current edge. For information only - deleted edges are counted.
    size_t DepIndex() const {
        return DepIter.Index();
    }

    /// @brief returns number of edges. For information only - deleted edges are counted.
    size_t NumDeps() const {
        return DepIter.Total();
    }

    /// @brief Check that Node is valid (was not deleted)
    bool IsValid() const {
        return Node().IsValid();
    }

    /// @brief Mark current dependency as accepted
    /// It is expected that all edges beneath the top are accepted
    /// Top edge is accepted in Leave() and Left() notifications,
    /// and not accepted in Enter(), AcceptDep() and iteration loop body.
    /// In all latter cases top edge is one that will be visited next
    void AcceptDep() {
        AcceptedDep = true;
    }

    /// @brief Check whether state item has accepted dependency
    bool IsDepAccepted() const {
        return AcceptedDep;
    }

    /// @brief retrieve name of the current node
    /// Slow - use only for dumping and debugging
    TFileView GetFileName() const {
        return TGraph::GetFileName(Node());
    }

    TCmdView GetCmdName() const {
        return TGraph::GetCmdName(Node());
    }

    /// @brief print current node name by default
    TString Print() const {
        return TGraph::ToString(Node());
    }
};

using TGraphConstIteratorStateItemBase = TGraphIteratorStateItemBase<true>;

/// @brief Template to convert some payload data (TStackData)
///        to valid depth-first traversal state
template <typename TStackData, bool IsConst = false, typename TGraph = TDepGraph>
class TGraphIteratorStateItem: public TGraphIteratorStateItemBase<IsConst, TGraph>, public TStackData {
private:
    using TBase = TGraphIteratorStateItemBase<IsConst, TGraph>;

public:
    explicit TGraphIteratorStateItem(const typename TBase::TDepRef& dep)
        : TBase(dep)
        , TStackData()
    {
    }
    explicit TGraphIteratorStateItem(const typename TBase::TNodeRef& node)
        : TBase(node)
        , TStackData()
    {
    }
};

/// @brief This is base class of the state for the actual non-recursive DF graph traversal
template <typename TStateItem = TGraphIteratorStateItemBase<false>>
class TGraphIteratorStateBase {
public:
    using TStackType = TVector<TStateItem>;
    using TItem = TStateItem;
    using TIterator = typename TStackType::reverse_iterator;
    using TConstIterator = typename TStackType::const_reverse_iterator;
    using TNodeRef = typename TStateItem::TNodeRef;
    using TDepRef = typename TStateItem::TDepRef;

private:
    TStackType State;

public:
    explicit TGraphIteratorStateBase() {
    }

    TStackType& Stack() {
        return State;
    }

    const TStackType& Stack() const {
        return State;
    }

    /// @brief Retrieve current top state item
    TStateItem& Top() {
        Y_ASSERT(!IsEmpty());
        return State.back();
    }

    /// @brief Retrieve current top state item
    const TStateItem& Top() const {
        Y_ASSERT(!IsEmpty());
        return State.back();
    }

    /// @brief Push state to the top
    ///        Allows supplying any arguments that TStateItem's c'tor may accept
    template <typename... Args>
    void Push(Args&&... args) {
        bool first = IsEmpty();
        State.emplace_back(std::forward<Args>(args)...);
        State.back().IsStart = first;
    }

    /// @brief Pop the state item from the stack during backtracking
    void Pop() {
        Y_ASSERT(!IsEmpty());
        State.pop_back();
    }

    /// @brief Check that state stack is empty
    bool IsEmpty() const {
        return State.empty();
    }

    /// @brief Retieve iterator pointing to the _top_ of the stack
    ///        Normal iteration order of a state is reverse from newest to oldest
    TIterator begin() {
        return State.rbegin();
    }

    /// @brief Retieve iterator pointing _beyond_the_bottom_ of the stack
    ///        Normal iteration order of a state is reverse from newest to oldest
    TIterator end() {
        return State.rend();
    }

    /// @brief Retieve iterator pointing to the _top_ of the stack
    ///        Normal iteration order of a state is reverse from newest to oldest
    TConstIterator begin() const {
        return State.crbegin();
    }

    /// @brief Retieve iterator pointing _beyond_the_bottom_ of the stack
    ///        Normal iteration order of a state is reverse from newest to oldest
    TConstIterator end() const {
        return State.crend();
    }

    /// @brief Returns size of the stack
    size_t Size() const {
        return State.size();
    }

    /// @brief Returns iterator to the item below the top
    TIterator Parent() {
        return begin() + 1;
    }

    /// @brief Returns iterator to the item below the top
    TConstIterator Parent() const {
        return begin() + 1;
    }

    /// @brief Retrieves Node from the top
    TNodeRef TopNode() {
        return Top().Node();
    }

    /// @brief Retrieves Node from the top
    const TNodeRef TopNode() const {
        return Top().Node();
    }

    /// @brief Retrieves parent Node for the top
    ///        parent Node is the one under it on the stack
    const TNodeRef ParentNode() const {
        return Size() >= 2 ? (*++begin()).Node() : TDepGraph::GetInvalidNode(Top().Node());
    }

    /// @brief check availability of the next dependency
    bool AtEnd() const {
        return Top().AtEnd();
    }

    /// @brief Return dependency edge at the top of stack
    /// This dependency will be visited next
    TDepRef NextDep() {
        return Top().CurDep();
    }

    /// @brief Return dependency edge at the top of stack
    /// This dependency will be visited next
    const TDepRef NextDep() const {
        return Top().CurDep();
    }

    /// @brief Return dependency edge led to current node
    const TDepRef IncomingDep() const {
        return HasIncomingDep() ? (*++begin()).CurDep() : TStateItem::TGraph::GetInvalidEdge(Top().Node());
    }

    /// @brief Return dependency edge led to current node
    TDepRef IncomingDep() {
        return HasIncomingDep() ? (*++begin()).CurDep() : TStateItem::TGraph::GetInvalidEdge(Top().Node());
    }

    /// @brief returns true if current state is brough by some edge
    /// This is false for initial node (either at stack bottom or from some nested iteration)
    bool HasIncomingDep() const {
        return Size() >= 2 && (*++begin()).IsDepAccepted();
    }

    /// @brief Find the topmost node matching the criteria supplied
    /// The criteria should be callable as bool(const TStateItem&)
    template <typename TMatch>
    TIterator FindRecent(TMatch match) {
        return FindRecent(begin(), match);
    }

    /// @brief Find the topmost node matching the criteria supplied starting from given position
    /// The criteria should be callable as bool(const TStateItem&)
    template <typename TMatch>
    TIterator FindRecent(TIterator from, TMatch match) {
        return std::find_if(from, end(), match);
    }

    /// @brief Find the topmost node matching the criteria supplied
    /// The criteria should be callable as bool(const TStateItem&)
    template <typename TMatch>
    TConstIterator FindRecent(TMatch match) const {
        return FindRecent(begin(), match);
    }

    /// @brief Find the topmost node matching the criteria supplied starting from given position
    /// The criteria should be callable as bool(const TStateItem&)
    template <typename TMatch>
    TConstIterator FindRecent(TConstIterator from, TMatch match) const {
        return std::find_if(from, end(), match);
    }

    void Dump() const {
        YDIAG(Dev) << "ITERATOR STACK DUMP" << Endl;

        size_t n = Size();
        for (const auto& item : *this) {
            YDIAG(Dev) << "STACK[" << (--n) << "] = " << item.Print() << Endl;
        }
    }
#if defined(__GNUC__) && !defined(NDEBUG)
#define EMIT_DBG __attribute__((used))
#else
#define EMIT_DBG
#endif
    void EMIT_DBG DumpW() const {
        YWarn() << "ITERATOR STACK DUMP" << Endl;
        size_t n = Size();
        for (const auto& item : *this) {
            YWarn() << "STACK[" << (--n) << "] = " << item.Print() << Endl;
        }
    }
};
using TGraphConstIteratorState = TGraphIteratorStateBase<TGraphConstIteratorStateItemBase>;

struct TVisitorStateItemBaseDebug {
    template<typename... TArgs>
    TVisitorStateItemBaseDebug(const TArgs&... args) {
        Y_UNUSED(args...);
    }
};

/// @brief The most basic state needed for TNoReentryVisitorBase
struct TVisitorStateItemBase {
    using TItemDebug = TVisitorStateItemBaseDebug;

    bool InStack;
    explicit TVisitorStateItemBase(TItemDebug itemDebug = {}, bool inStack = false)
        : InStack(inStack)
    {
        Y_UNUSED(itemDebug);
    }
};

/// @brief Template to convert some payload data (TNodeData)
///        to valid Fiter State
template <typename TNodeData>
struct TVisitorStateItem: public TVisitorStateItemBase, TNodeData {
    explicit TVisitorStateItem(TItemDebug itemDebug, bool inStack = false)
        : TVisitorStateItemBase(itemDebug, inStack)
        , TNodeData()
    {
    }

    template <typename... Args>
    explicit TVisitorStateItem(TItemDebug itemDebug, bool inStack, Args... nodeArgs)
        : TVisitorStateItemBase(itemDebug, inStack)
        , TNodeData(std::forward<Args>(nodeArgs)...)
    {
    }
};

/// @brief the most basic visitor: implements required minimal interface, while doing noting
/// This one is needed as a possible base class for TLogVisitor<Base>
template <typename TStateItem_ = TGraphIteratorStateItemBase<>, typename TIterState = TGraphIteratorStateBase<TStateItem_>>
struct TBaseVisitor {
    using TState = TIterState;
    using TStateItem = TStateItem_;
    static constexpr bool IsConst = TStateItem::IsConst;

    bool Enter(TState&) {
        return true;
    }
    void Leave(TState&) {
        return;
    }
    void Left(TState&) {
        return;
    }
    bool AcceptDep(TState&) {
        return true;
    }
    void Reset() {
    }
};

template <typename TVisitorStateItem>
struct TVisitorFreshTraits {
    static bool IsFresh(const TVisitorStateItem& item, bool wasJustCreated) {
        Y_UNUSED(item);
        return wasJustCreated;
    }
};

/// @brief The most basic No Reentry visitor
///        It implements no-reentry policy for depth-firts traversal
template <typename TVisitorStateItem = TVisitorStateItemBase,
          typename TIterStateItem = TGraphIteratorStateItemBase<>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
class TNoReentryVisitorBase {
protected:
    using TNodes = THashMap<TNodeId, TVisitorStateItem>;

    TNodes Nodes;
    TVisitorStateItem* CurEnt;

public:
    using TState = TIterState;
    using TStateItem = TIterStateItem;
    static constexpr bool IsConst = TStateItem::IsConst;

    TNoReentryVisitorBase() = default;

    /// @brief Set visitor to clear state for reuse
    void Reset() {
        Nodes.clear();
        CurEnt = nullptr;
    }

    /// @brief This method is called when new node just pushed onto stack
    ///        we may accept or reject node (it will be Popped immediately in latter case)
    ///
    /// We allow node if it is valid and never seen before (not present in Nodes)
    /// If we have seen node before we deny it, but point CurEnt and Top.Cookie to its
    /// firts instance kept in Nodes
    bool Enter(TState& state) {
        const auto& node = state.TopNode();
        if (!node.IsValid()) {
            CurEnt = nullptr;
            return false;
        }

        auto [i, fresh] = Nodes.try_emplace(node.Id(), typename TVisitorStateItem::TItemDebug{node}, true);
        fresh = TVisitorFreshTraits<TVisitorStateItem>::IsFresh(i->second, fresh);
        i->second.InStack = true;
        state.Top().Cookie = CurEnt = &i->second;
        return fresh;
    }

    /// @brief This method is called when node is about to leave the stack
    ///        both in case of backtracking and Enter() denial.
    ///
    /// Enter may set CurEnt to NULL in case of invalid node
    void Leave(TState& /*state*/) {
        if (CurEnt != nullptr) {
            CurEnt->InStack = false;
        }
    }

    /// @brief This method is called when node just left the stack
    ///        and so we entered its parent
    ///
    /// Update CurEnt using Cookie to avoid lookups
    void Left(TState& state) {
        CurEnt = VisitorEntry(state.Top());
    }

    /// @brief This method is called when we are about to traverse the edge
    ///        We may allow or deny traversal.
    ///
    /// In order to make our decision we examine current edge in the state
    /// we allow edge if it points to never seen node or seen node which is
    /// not currently in stack. This allows processing repeating nodes outside loops
    bool AcceptDep(TState& state) {
        const auto& dep = state.NextDep();
        const auto& to = dep.To();
        typename TNodes::iterator i = Nodes.find(to.Id());
        return (i == Nodes.end() || !i->second.InStack);
    }

protected:
    /// @brief Retrieve visitor entry data from stack item
    ///
    /// Uses Cookie to avoid lookups
    static TVisitorStateItem* VisitorEntry(const TStateItem& stateItem) {
        return (TVisitorStateItem*)stateItem.Cookie;
    }
};

/// @brief The ymake's default payload for base NoReenty visitor
struct TEntryStatsData {
    union {
        ui8 AllFlags;
        struct {  // 4 bits used
            bool HasBuildFrom : 1;
            bool HasBuildCmd : 1;
            bool IsFile : 1;

            // a (temporary) means of supporting different graph representations
            // of variable context for old and new command styles
            bool StructCmdDetected : 1;
        };
    };
    explicit TEntryStatsData(bool isFile = false)
        : AllFlags(0)
    {
        IsFile = isFile;
    }
};

using TEntryStats = TVisitorStateItem<TEntryStatsData>;

/// @brief The ymake's base NoReentry visitor collects some basic data about nodes during traversal
template <typename TVisitorState = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
class TNoReentryStatsVisitor: public TNoReentryVisitorBase<TVisitorState, TIterStateItem, TIterState> {
public:
    using TBase = TNoReentryVisitorBase<TVisitorState, TIterStateItem, TIterState>;
    using TStateItem = TIterStateItem;
    using typename TBase::TState;

    /// @brief If node is permitted record whether is File node
    bool Enter(TState& state) {
        bool fresh = TBase::Enter(state);
        if (fresh) {
            CurEnt->IsFile = IsFileType(state.TopNode()->NodeType);
        }
        return fresh;
    }

    /// @brief On any edge to the node check that the edge is build command over file node
    ///        and record that the node has build command for it. Also record existence of
    ///        BuildFrom dependency
    bool AcceptDep(TState& state) {
        const auto& dep = state.NextDep();
        if (*dep == EDT_BuildFrom) {
            CurEnt->HasBuildFrom = true;
        } else if (*dep == EDT_BuildCommand && IsFileType(dep.From()->NodeType)) {
            CurEnt->HasBuildCmd = true;
        }
        return TBase::AcceptDep(state);
    }

protected:
    /// Template inheritance in C++ adds some ugliness: dependent names are not attributed to ancestor
    using TBase::Nodes;
    using TBase::CurEnt;
};

template <typename TVisitorStateItem = TEntryStats,
          typename TIterStateItem = TGraphIteratorStateItemBase<true>,
          typename TIterState = TGraphIteratorStateBase<TIterStateItem>>
using TNoReentryStatsConstVisitor = TNoReentryStatsVisitor<TVisitorStateItem, TIterStateItem, TIterState>;

/// @brief This is class for iteration algorithm with filtering by vistor
///
/// It is not expected to be inherited by users: one should inherit from
/// State and Visitor classes above to implement filtering and sophisticated state management
template <typename TIterState, typename TVisitor, typename TGraph = TDepGraph>
class TDepthGraphIterator final {
private:
    using TGraphRef = TAddRef<TGraph, TIterState::TItem::IsConst>;
    TGraphRef Graph_;
    TIterState& State_;
    TVisitor& Visitor_;
    ssize_t Bottom_;

    using TVisitorState = typename TVisitor::TState;

public:
    /// @brief construct iterator in uninitialized state.
    ///        Call Init() before any calls to Next() or Run()
    TDepthGraphIterator(TGraphRef graph, TIterState& state, TVisitor& vistor)
        : Graph_(graph)
        , State_(state)
        , Visitor_(vistor)
        , Bottom_(-1)
    {
    }

    /// @brief construct iterator in initialized state.
    ///        This polymorphic c'tor passes trailing arguments to TIterState c'tor
    /// Note that in order to put iterator into valid state this c'tor may call
    /// Visitor methods. If your visitor is throwing avoid using this c'tor
    template <typename... Args>
    TDepthGraphIterator(TGraphRef graph, TIterState& state, TVisitor& visitor, Args&&... start)
        : TDepthGraphIterator(graph, state, visitor)
    {
        Init(std::forward<Args>(start)...);
    }

    /// @brief construct iterator in initialized state for specific NodeId
    ///
    /// Note that in order to put iterator into valid state this c'tor may call
    /// Visitor methods. If your visitor is throwing avoid using this c'tor
    TDepthGraphIterator(TGraphRef graph, TNodeId start, TIterState& state, TVisitor& visitor)
        : TDepthGraphIterator(graph, graph[start], state, visitor)
    {
    }

    /// @brief Initialize iterator for node by NodeId
    ///        This function attempts to put initial state on stack and calls appropriate
    ///        notifications from the Visitor
    bool Init(TNodeId start) {
        return Init(Graph_[start]);
    }

    /// @brief Initialize iterator from any set of arguments acceptable for TIterState construction
    ///        This function attempts to put initial state on stack and calls appropriate
    ///        notifications from the Visitor
    template <typename... Args>
    bool Init(Args&&... args) {
        Y_ASSERT(!Initialized());

        Bottom_ = State_.Size();
        if (!Push(std::forward<Args>(args)...)) {
            Pop();
            return false;
        }
        return true;
    }

    /// @brief Run depth-first traversal and stop when next accepted node is reached by accepted edge
    /// @return true if node is found and else if traversal is finished and state is empty
    bool Next() {
        Y_ASSERT(Initialized());
        while (!Done()) {
            if (State().Top().IsDepAccepted() && State().TopNode().IsValid()) {
                // This is true after pop: dep on stack
                // represented incoming edge for the left node
                // se we need to advance to the next edge in this case
                State().Top().AdvanceDep();
            }
            while (IsDep()) {
                if (Visitor().AcceptDep(State())) {
                    // This marks edge on stack as accepted
                    // It is expected that all edges beneath the top are accepted
                    // Top edge is accepted in Leave() and Left() notifications,
                    // and not accepted in Enter(), AcceptDep() and iteration loop body.
                    State().Top().AcceptDep();
                    break;
                }
                State().Top().AdvanceDep();
            }
            if (IsDep()) {
                if (Push(State().Top().CurDep())) {
                    return true;
                }
            }
            Pop();
            if (Done()) {
                return false;
            }
        }
        return false;
    }

    /// @brief Run complete depth-first traversal till state is empty
    void Run() {
        Y_ASSERT(Initialized());
        while (Next()) {
        }
    }

    /// @brief Check whether we're done with the traversal
    bool Done() {
        Y_ASSERT(Initialized());
        return State().Size() == Bottom();
    }

    /// @brief Returns current state item if any
    typename TIterState::TItem& Top() {
        return State().Top();
    }

    /// @brief Returns current state item if any as const reference
    const typename TIterState::TItem& Top() const {
        return State().Top();
    }

    /// @brief Returns current state item if any
    typename TIterState::TItem& operator*() {
        return Top();
    }

    /// @brief Returns current state item if any as const reference
    const typename TIterState::TItem& operator*() const {
        return Top();
    }

    /// @brief Returns current state item for field access
    const typename TIterState::TItem* operator->() {
        return &Top();
    }

    /// @brief Returns current state item for constant field access
    const typename TIterState::TItem* operator->() const {
        return &Top();
    }

    /// @brief Retrieves the state of the iterator.
    ///        May come handy if iterator is passed around
    TIterState& State() {
        return State_;
    }

    /// @brief Retrieves the read-only state of the iterator.
    ///        May come handy if iterator is passed around
    const TIterState& State() const {
        return State_;
    }

    /// @brief Retrieves the Visitor set for the iterator.
    ///        May come handy if iterator is passed around
    TVisitor& Visitor() {
        return Visitor_;
    }

    /// @brief Retrieves the read-only Visitor set for the iterator.
    ///        May come handy if iterator is passed around
    const TVisitor& Visitor() const {
        return Visitor_;
    }

    ~TDepthGraphIterator() {
        if (UncaughtException() && !Done()) {
            State_.Dump();
        }
    }

private:
    bool Initialized() const {
        return Bottom_ >= 0;
    }

    size_t Bottom() const {
        Y_ASSERT(Initialized());
        return (size_t)Bottom_;
    }

    /// @brief Shortcut to check that we not reached end of edges list
    ///        also avoid visiting deps from and to deleted nodes
    bool IsDep() const {
        return State().Top().IsValid() && !State().Top().AtEnd() &&
               State().NextDep().IsValid() &&
               State().NextDep().To().IsValid();
    }

    /// @brief Push the state item and notify the visitor
    template <typename... Args>
    bool Push(Args&&... args) {
        State().Push(std::forward<Args>(args)...);
        if (State().TopNode().IsValid()) {
            return Visitor().Enter(State());
        } else {
            return false;
        }
    }

    /// @brief Pop the state item and notify the visitor
    void Pop() {
        if (State().TopNode().IsValid()) {
            Visitor().Leave(State());
        }
        State().Pop();

        if (!State().IsEmpty() && State().TopNode().IsValid()) {
            Visitor().Left(State());
        }
    }
};

//////////////  Set of IterateAll functions: they will construct iterator internally and run the traversal //////////

/// @brief default collection filtering for IterateAll() with collections
struct TDefaultFilter {
    template <typename T>
    bool operator()(T) {
        return true;
    }
};

/// @brief Iterate all dependants of the node using supplied State and Visitor objects
///
/// Note: to avoid exceptions in c'tor this code calls Init() separately
template <typename TIterState, typename TVisitor,
          typename = typename std::enable_if<std::is_base_of<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value ||
                                             std::is_same<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value>::type>
inline void IterateAll(typename TDepGraph::TAnyNodeRef<TIterState::TItem::IsConst> node, TIterState& state, TVisitor& visitor) {
    TDepthGraphIterator<TIterState, TVisitor> it(TDepGraph::Graph(node), state, visitor);
    if (it.Init(node)) {
        it.Run();
    }
}

/// @brief Iterate all dependants of the node with give Id from given graph using supplied State and Visitor objects
template <typename TIterState,
        typename TVisitor,
        typename = typename std::enable_if<std::is_base_of<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value ||
                                           std::is_same<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst>, TDepGraph::TAnyNodeRef<TIterState::TItem::IsConst> node, TIterState& state, TVisitor& visitor) {
    IterateAll(node, state, visitor);
}

/// @brief Iterate all dependants of the node with give Id from given graph using supplied State and Visitor objects
template <typename TIterState,
          typename TVisitor,
          typename = typename std::enable_if<std::is_base_of<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value ||
                                             std::is_same<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph, TNodeId node, TIterState& state, TVisitor& visitor) {
    IterateAll(graph[node], state, visitor);
}

/// @brief Iterate over all nodes of the graph using supplied State and Visitor objects
template <typename TIterState,
          typename TVisitor,
          typename = typename std::enable_if<std::is_base_of<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value ||
                                             std::is_same<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph, TIterState& state, TVisitor& visitor) {
    for (auto node_it = graph.Nodes().begin(); node_it != graph.Nodes().end(); ++node_it) {
        IterateAll(*node_it, state, visitor);
    }
}

/// @brief Iterate over set of nodes provided in vector of any type convertible to TNodeId
///        and pre-filtered by supplied nodesFilter. Use supplied State and Visitor objects.
template <typename TIterState,
          typename TVisitor,
          typename TContainer,
          typename TFunc = TDefaultFilter,
          typename = typename std::enable_if<std::is_base_of<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value ||
                                             std::is_same<TGraphIteratorStateBase<typename TIterState::TItem>, TIterState>::value>::type,
          typename TItemRef = decltype(*std::begin(std::declval<const TContainer>())),
          typename = decltype(std::declval<TFunc>()(std::declval<TItemRef>()))>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph,
                       const TContainer& nodes,
                       TIterState& state,
                       TVisitor& visitor,
                       TFunc nodesFilter = TDefaultFilter{}) {
    for (auto node_it = std::begin(nodes); node_it != std::end(nodes); ++node_it) {
        if (nodesFilter(*node_it)) {
            IterateAll(graph, *node_it, state, visitor);
        }
    }
}

/// ==== With default constructed State ====

/// @brief Iterate all dependants of the node
///        using supplied Visitor object and internally constructed State object
/// @example IterateAll<TMyState>(node, myVisitor);
template <typename TVisitor,
          typename TIterState = typename TVisitor::TState,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(typename TDepGraph::TAnyNodeRef<TIterState::TItem::IsConst> node, TVisitor& visitor) {
    TIterState state;
    IterateAll(node, state, visitor);
}

/// @brief Iterate all dependants of the node with give Id from given graph
///        using supplied Visitor object and internally constructed State object
template <typename TVisitor,
          typename TIterState = typename TVisitor::TState,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph, TNodeId node, TVisitor& visitor) {
    TIterState state;
    IterateAll(graph, node, state, visitor);
}

/// @brief Iterate over all nodes of the graph
///        using supplied Visitor object and internally constructed State object
template <typename TVisitor,
          typename TIterState = typename TVisitor::TState,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph, TVisitor& visitor) {
    TIterState state;
    IterateAll(graph, state, visitor);
}

/// @brief Iterate over set of nodes provided in vector of any type convertible to TNodeId
///        and pre-filtered by supplied nodesFilter.
///        Use supplied Visitor object and internally constructed State object
template <typename TVisitor,
          typename TIterState = typename TVisitor::TState,
          typename TContainer,
          typename TFunc = TDefaultFilter,
          typename TItemRef = decltype(*std::begin(std::declval<const TContainer>())),
          typename = decltype(std::declval<TFunc>()(std::declval<TItemRef>())),
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph,
                       const TContainer& nodes,
                       TVisitor& visitor,
                       TFunc nodesFilter = TDefaultFilter{}) {
    TIterState state;
    IterateAll(graph, nodes, state, visitor, nodesFilter);
}

/// ==== With default constructed Visitor and State ====

/// @brief Iterate all dependants of the node
///        using internally constructed visitor and state object
///        Note: visitor expects particular state, so defining visitor class is enough
/// @example IterateAll<TMyVisitor>(node);
template <typename TVisitor = TNoReentryStatsVisitor<>,
          typename TIterState = TGraphIteratorStateBase<typename TVisitor::TStateItem>,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(typename TDepGraph::TAnyNodeRef<TIterState::TItem::IsConst> node) {
    TIterState state;
    TVisitor visitor;
    IterateAll(node, state, visitor);
}

/// @brief Iterate all dependants of the node with give Id from given graph
///        using internally constructed visitor and state object
template <typename TVisitor = TNoReentryStatsVisitor<>,
          typename TIterState = TGraphIteratorStateBase<typename TVisitor::TStateItem>,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph, TNodeId node) {
    TIterState state;
    TVisitor visitor;
    IterateAll(graph, node, state, visitor);
}

/// @brief Iterate over all nodes of the graph
///        using internally constructed visitor and state object
template <typename TVisitor = TNoReentryStatsVisitor<>,
          typename TIterState = TGraphIteratorStateBase<typename TVisitor::TStateItem>,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph) {
    TIterState state;
    TVisitor visitor;
    IterateAll(graph, state, visitor);
}

/// @brief Iterate over set of nodes provided in vector of any type convertible to TNodeId
///        and pre-filtered by supplied nodesFilter.
///        Use internally constructed visitor and state object
template <typename TVisitor = TNoReentryStatsVisitor<>,
          typename TIterState = TGraphIteratorStateBase<typename TVisitor::TStateItem>,
          typename = typename std::enable_if<std::is_same<typename TVisitor::TState, TIterState>::value>::type,
          typename TContainer,
          typename TFunc = TDefaultFilter,
          typename TItemRef = decltype(*std::begin(std::declval<const TContainer>())),
          typename = decltype(std::declval<TFunc>()(std::declval<TItemRef>()))>
inline void IterateAll(TAddRef<TDepGraph, TIterState::TItem::IsConst> graph,
                       const TContainer& nodes,
                       TFunc nodesFilter = TDefaultFilter{}) {
    TIterState state;
    TVisitor visitor;
    IterateAll(graph, nodes, state, visitor, nodesFilter);
}

/// @brief Target node decription
///        This struct is convertible to NodeId and so may be supplied to IterateAll functions
struct TTarget {
    TNodeId Id;
    union {
        ui32 AllFlags;
        struct {
            ui32 IsNonDirTarget : 1;
            ui32 IsUserTarget : 1;
            ui32 IsRecurseTarget : 1;
            ui32 IsDependsTarget : 1;
            ui32 IsDepTestTarget: 1;
            ui32 IsModuleTarget : 1;
        };
    };

    TString Tag;

    TTarget(TNodeId id, ui32 allFlags = 0, TString tag = "")
        : Id(id)
        , AllFlags(allFlags)
        , Tag(tag)

    {
    }

    operator TNodeId() const {
        return Id;
    }
};

template<>
struct hash<TTarget>: hash<TNodeId> {};
