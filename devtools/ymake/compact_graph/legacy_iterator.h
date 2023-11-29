#pragma once

#include "dep_types.h"
#include "iter.h"

template <class TIterState_>
class TDepthDGIter {
public:
    typedef TIterState_ TIterState;
    typedef TVector<TIterState> TState;
    TState State;
    TDepGraph& Graph;

protected:
    typename TIterState::DepIter StartIter;     // only used in first call to Next()
    typename TIterState::DepIter* StartIterPtr; // only used in first call to Next()
    TNodeId StartNodeId;

public:
    struct TEntryStats {
        bool InStack;
        bool HasBuildFrom;
        bool HasBuildCmd;
        bool IsFile;
        TEntryStats(bool inStack = false, bool isFile = false)
            : InStack(inStack)
            , HasBuildFrom(0)
            , HasBuildCmd(0)
            , IsFile(isFile)
        {
        }
    };

    enum class EDepVerdict { No, Yes, Delay };

    TDepthDGIter(TDepGraph& graph, TNodeId startNodeOffs = 0)
        : Graph(graph)
        , StartIter(Graph, 0)
        , StartNodeId(startNodeOffs)
    {
        Restart(startNodeOffs);
    }

    virtual ~TDepthDGIter() {
        if (UncaughtException() && !State.empty())
            DumpStack();
    }

    void Restart(TNodeId nodeId) {
        StartIter = typename TIterState::DepIter(Graph, nodeId);
        StartIterPtr = nodeId ? &StartIter : nullptr;
        StartNodeId = nodeId;
    }

    void Restart(const typename TIterState::DepIter& start) {
        StartIter = start;
        StartIterPtr = &StartIter;
        StartNodeId = 0;
    }

    // Next logic:
    //
    // def recur(proc, node):
    //     if proc.Enter(node):
    //         yield node
    //         for dep in node.deps:
    //             if proc.AcceptDep(node, dep):
    //                 yield from recur(proc, dep)
    //                 proc.Left(node, dep)
    //     proc.Leave(node)
    template <class TNodeProc>
    bool Next(TNodeProc& proc) {
        if (Y_UNLIKELY(StartIterPtr)) {
            if (StartNodeId) {
                State.push_back(TIterState(Graph, StartNodeId));
            } else {
                State.push_back(TIterState(Graph, *StartIterPtr));
            }

            StartIterPtr = nullptr;
            if (proc.Enter(State)) {
                State.back().InitDeps();
                return true;
            }
            proc.Leave(State);
            State.pop_back();
        }
        if (State.empty())
            return false;
        while (true) {
            TIterState& st = State.back();
            for (; !st.AtEnd(); st.CurDep++) {
                st.NextDep();
                const auto verdict = proc.AcceptDep(State);
                if (verdict == EDepVerdict::Yes) // TODO: real filter here
                    break;
                if (verdict == EDepVerdict::Delay) {
                    st.DelayCurrentDep();
                }
            }
            if (!st.AtEnd() || st.HasDelayedDeps()) {
                if (!st.AtEnd()) {
                    st.CurDep++;
                    State.push_back(TIterState(Graph, st.Dep));
                } else {
                    State.push_back(TIterState(Graph, st.PopDelayedDep()));
                }
                if (proc.Enter(State)) { // returns true if node is accepted
                    State.back().InitDeps();
                    break;
                }
            }
            proc.Leave(State);
            State.pop_back();
            if (State.empty())
                return false;
            proc.Left(State);
        }
        return true;
    }

    template <class TNodeProc>
    void IterateAll(TNodeProc& proc, const TVector<TTarget>& nodes, bool (*filter)(const TTarget&) = [](const TTarget&) -> bool { return true; }) {
        Y_ASSERT(!StartIterPtr);
        for (size_t n = 0; n < nodes.size(); n++) {
            if (filter(nodes[n])) {
                Restart(nodes[n].Id);
                while (Next(proc)) {
                }
            }
        }
    }

    template <class TNodeProc>
    void IterateAll(TNodeProc& proc, const TVector<ui64>& nodes) {
        Y_ASSERT(!StartIterPtr);
        for (size_t n = 0; n < nodes.size(); n++) {
            Restart(nodes[n]);
            while (Next(proc)) {
            }
        }
    }

    TNodeId CurNodeOffs() const {
        return Graph.GetNodeById(State.back().Node.NodeType, State.back().Node.ElemId).Id();
    }

    TNodeId ParentNodeOffs() const {
        return State.size() < 2 ? 0 : Graph.GetNodeById(State[State.size() - 2].Node.NodeType, State[State.size() - 2].Node.ElemId).Id();
    }

    const TDepTreeNode& CurNode() const {
        return State.back().Node;
    }

    const TDepTreeNode* ParentNode() const {
        return State.size() < 2 ? 0 : &State[State.size() - 2].Node;
    }

    EDepType DepType() const {
        return State.size() < 2 ? EDT_Search : State[State.size() - 2].Dep.DepType;
    }

    const TString GetName() const {
        return Graph.ToString(CurNode());
    }

    TIterState* GetStateByDepth(size_t depth = 0) {
        ssize_t index = static_cast<ssize_t>(State.size()) - static_cast<ssize_t>(depth) - 1;
        if (index < 0) {
            return nullptr;
        }
        return &State[index];
    }

    void DumpStack() const {
        YDIAG(Dev) << "ITERATOR STACK DUMP" << Endl;
        for (size_t n = State.size(); n > 0; n--) {
            YDIAG(Dev) << "STACK[" << (n - 1) << "] = " << State[n - 1].Print(Graph) << Endl;
        }
    }

#if defined(__GNUC__) && !defined(NDEBUG)
#define EMIT_DBG __attribute__((used))
#else
#define EMIT_DBG
#endif

    void EMIT_DBG DumpStackW() const {
        YWarn() << "ITERATOR STACK DUMP" << Endl;
        for (size_t n = State.size(); n > 0; n--) {
            YWarn() << "STACK[" << (n - 1) << "] = " << State[n - 1].Print(Graph) << Endl;
        }
    }
};
