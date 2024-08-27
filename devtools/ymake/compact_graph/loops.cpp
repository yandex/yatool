#include "loops.h"

#include "iter.h"
#include "query.h"

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <util/generic/hash_set.h>
#include <util/stream/format.h>

#include <fmt/format.h>

struct TLoopId {
    TNodeId LoopId;
    bool IsFile;

    TLoopId(bool isFile = false)
        : LoopId(TNodeId::Invalid)
        , IsFile(isFile)
    {
    }
};

using TLoopData = TVisitorStateItem<TLoopId>;

class TLoopSearcher: public TNoReentryStatsVisitor<TLoopData, TGraphConstIteratorStateItemBase> {
public:
    using TBase = TNoReentryStatsVisitor<TLoopData, TGraphConstIteratorStateItemBase>;

    // Disjoint-set with path compression and union by rank
    class TGluedLoops {
    private:
        struct TLoopData {
            TNodeId ParentLoop = TNodeId::Invalid;
            size_t Rank = 0;
        };

        TVector<TLoopData> Loops;

    public:
        TGluedLoops()
            : Loops({TLoopData()})
        {}

        TNodeId AddLoop() {
            Loops.emplace_back();
            return static_cast<TNodeId>(Loops.size() - 1);
        }

        TNodeId GetLoopId(TNodeId loopId) {
            TVector<TNodeId> pathToRoot;

            while (Loops[AsIdx(loopId)].ParentLoop > TNodeId::Invalid) {
                pathToRoot.push_back(loopId);
                loopId = Loops[AsIdx(loopId)].ParentLoop;
            }

            for (const auto& loopNum : pathToRoot) {
                Loops[AsIdx(loopNum)].ParentLoop = loopId;
            }

            return loopId;
        }

        void JoinLoops(TNodeId first, TNodeId second) {
            first = GetLoopId(first);
            second = GetLoopId(second);
            if (first == second) {
                return;
            }

            if (Loops[AsIdx(first)].Rank < Loops[AsIdx(second)].Rank) {
                std::swap(first, second);
            } else if (Loops[AsIdx(first)].Rank == Loops[AsIdx(second)].Rank) {
                Loops[AsIdx(first)].Rank++;
            }
            Loops[AsIdx(second)].ParentLoop = first;
        }
    };

    bool OutTogetherIsLoop;
    TGluedLoops GluedLoops;
    using TBase::Nodes;

public:
    explicit TLoopSearcher(bool outTogetherIsLoop)
        : OutTogetherIsLoop(outTogetherIsLoop)
    {
    }

    bool AcceptDep(TState& state) {
        const auto& dep = state.NextDep();
        if (!IsLoopGenDep(dep)) {
            return false;
        }
        TNodes::iterator i = Nodes.find(dep.To().Id());
        bool acc = i == Nodes.end();
        if (i != Nodes.end() && (i->second.InStack || i->second.LoopId != TNodeId::Invalid)) { // <=> i && (i->second.InStack || i->second.LoopId)
            TNodeId loopNodeStart = dep.To().Id();

            if (loopNodeStart == dep.From().Id()) { // link to self does not create loop
                return acc;
            }

            TNodeId knownLoopId = !i->second.InStack ? i->second.LoopId : TNodeId::Invalid;

            if (knownLoopId != TNodeId::Invalid) {
                auto found = state.FindRecent(
                    [knownLoopId, this](const TStateItem& item) {
                        return GluedLoops.GetLoopId(((TLoopData*)item.Cookie)->LoopId) == GluedLoops.GetLoopId(knownLoopId);
                    });
                if (found == state.end()) {
                    return acc;
                } else {
                    const auto& node = found->Node();
                    YDIAG(Loop) << "Previous entrance in loop on " << TDepGraph::ToString(node) << Endl;
                    loopNodeStart = node.Id();
                }
            }

            if (i->second.InStack) {
                // If we have OutTogether, first ensure that each of them is followed by BuildFrom.
                // This check is optimistic: among parallel edges only one BuildFrom is enough to form the loop
                // We are not in a loop unless they are (otherwise, incidentally, the loop is bad)
                // If only 1 item in state this code works fine detecting `bad` condition if dep is BuildFrom

                auto isNotCycle = [](EDepType currEdge, EDepType nextEdge) -> bool {
                    return currEdge == EDT_OutTogether && nextEdge != EDT_BuildFrom && nextEdge != EDT_Include;
                };

                auto reportNotCycle = [](TDepGraph::TConstEdgeRef currEdge, EDepType nextEdge) {
                    YDIAG(Loop) << "OutTogether not followed by BuildFrom or Include\n";
                    YDIAG(Loop)
                        << TDepGraph::ToString(currEdge.From()) << " (" << currEdge.From().Id() << ")"
                        << " --OutTogether--> "
                        << TDepGraph::ToString(currEdge.To()) << " (" << currEdge.To().Id() << ")"
                        << " --" << nextEdge << "--> ...\n";
                };

                EDepType lastEdgeType = *dep;
                EDepType currEdgeType = *dep;

                for (auto it = state.begin() + 1, end = state.end(); it != end; ++it) {
                    const auto& currEdge = (*it).CurDep();
                    EDepType nextEdgeType = currEdgeType;
                    currEdgeType = *currEdge;

                    if (!OutTogetherIsLoop && isNotCycle(currEdgeType, nextEdgeType)) {
                        reportNotCycle(currEdge, nextEdgeType);
                        return acc;
                    }

                    if (loopNodeStart == currEdge.From().Id()) {
                        if (isNotCycle(lastEdgeType, currEdgeType)) {
                            reportNotCycle(currEdge, nextEdgeType);
                            return acc;
                        }
                        break;
                    }
                }
            }
            // set up loop id (overlapped loops get glued into one big loop)
            TNodeId loopId = TNodeId::Invalid;
            auto found = state.FindRecent(
                [&loopId, loopNodeStart, this](const TStateItem& item) {
                    TLoopData* ent = (TLoopData*)item.Cookie;
                    if (ent->LoopId != TNodeId::Invalid) {
                        if (loopId == TNodeId::Invalid) {
                            loopId = ent->LoopId;
                        } else {
                            GluedLoops.JoinLoops(loopId, ent->LoopId);
                        }
                    }
                    return loopNodeStart == item.Node().Id();
                });
            Y_ASSERT(found != state.end());

            if (loopId == TNodeId::Invalid) {
                loopId = GluedLoops.AddLoop();
            }
            for (auto it = state.begin(), end = ++found; it != end; ++it) {
                TLoopData* ent = (TLoopData*)(*it).Cookie;
                ent->LoopId = loopId;
            }
        }
        return acc;
    }

    void CollectLoops(THashMap<TNodeId, TNodeId>& node2Loop, TVector<TGraphLoop>& loop2Nodes) {
        TVector<std::pair<TNodeId, TNodeId>> loopSrt;
        for (const auto& node : Nodes) {
            TNodeId loopId = node.second.LoopId;
            if (loopId != TNodeId::Invalid) {
                loopId = GluedLoops.GetLoopId(loopId);
                loopSrt.push_back(std::make_pair(loopId, node.first));
            }
        }
        if (loopSrt.empty()) {
            return;
        }
        std::sort(loopSrt.begin(), loopSrt.end());
        // continuous loop enumeration makes other code simpler
        TNodesData<size_t, TVector> loopSz;
        TNodeId newId, curId = loopSrt[0].first;
        loopSz.push_back(0);
        for (size_t n = 0; n < loopSrt.size(); n++) {
            if (curId != loopSrt[n].first) {
                loopSz.push_back(0);
                curId = loopSrt[n].first;
            }
            loopSz.back()++;
        }
        loop2Nodes.resize(loopSz.size());
        // loop id 0 is reserved for 'no loop' flag
        newId = TNodeId::MinValid, curId = loopSrt[0].first;
        for (size_t n = 0; n < loopSrt.size(); n++) {
            if (curId != loopSrt[n].first) {
                newId++;
                loop2Nodes[AsIdx(newId)].reserve(loopSz[newId]);
                curId = loopSrt[n].first;
            }
            node2Loop[loopSrt[n].second] = newId;
            loop2Nodes[AsIdx(newId)].push_back(loopSrt[n].second);
        }
        Y_ASSERT(newId == loopSz.MaxNodeId());
        YDIAG(Loop) << "Found " << newId << " loops, with " << loopSrt.size() << " elements (of " << Nodes.size() << " total nodes)" << Endl;
    }
};

bool TGraphLoops::HasBadLoops() const {
    return !DirLoops.empty() || !BuildLoops.empty();
}

void TGraphLoops::FindLoops(const TDepGraph& graph, const TVector<TTarget>& startTargets, bool outTogetherIsLoop) {
    TLoopSearcher ls(outTogetherIsLoop);
    clear();
    Node2Loop.clear();
    DirLoops.clear();
    BuildLoops.clear();
    IterateAll(graph, startTargets, ls, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
    ls.CollectLoops(Node2Loop, *this);

    for (size_t loopNum = 1; loopNum < size(); ++loopNum) {
        TGraphLoop curLoop = (*this)[loopNum];

        bool isDirLoop = false;
        bool isBuildLoop = false;

        THashSet<TNodeId> nodesInLoop(curLoop.begin(), curLoop.end());

        for (const auto& curNode : curLoop) {
            const auto& node = graph.Get(curNode);

            if (!isDirLoop && node->NodeType == EMNT_Directory) {
                DirLoops.insert(static_cast<TNodeId>(loopNum));
                isDirLoop = true;
            }

            if (!isBuildLoop) {
                for (const auto& edge : node.Edges()) {
                    if (*edge == EDT_BuildFrom && nodesInLoop.contains(edge.To().Id())) {
                        BuildLoops.insert(static_cast<TNodeId>(loopNum));
                        isBuildLoop = true;
                        break;
                    }
                }
            }

            if (isDirLoop && isBuildLoop) {
                break;
            }
        }
    }
}

void TGraphLoops::DumpAllLoops(const TDepGraph& graph, IOutputStream& out) {
    TVector<TNodeId> loops;
    for (TNodeId loopNum = TNodeId::MinValid; AsIdx(loopNum) < size(); ++loopNum) {
        loops.push_back(loopNum);
    }
    if (loops.size() != 0) {
        DumpLoops(graph, out, loops);
    } else {
        out << "Loops were not detected" << Endl;
    }
}

void TGraphLoops::DumpDirLoops(const TDepGraph& graph, IOutputStream& out) {
    DumpLoops(graph, out, DirLoops);
}

void TGraphLoops::DumpBuildLoops(const TDepGraph& graph, IOutputStream& out) {
    DumpLoops(graph, out, BuildLoops);
}

template <typename TContainer>
void TGraphLoops::DumpLoops(const TDepGraph& graph, IOutputStream& out, const TContainer& loopIds) {
    const auto& isLoopBad = [this](TNodeId loopId) {
        return DirLoops.contains(loopId) || BuildLoops.contains(loopId);
    };

    size_t loopNumber = 1;
    for (const auto& loopId : loopIds) {
        const TGraphLoop& curLoop = (*this)[AsIdx(loopId)];

        TStringStream ss;
        ss << "Loop " << loopNumber++ << " (size: " << curLoop.size() << (isLoopBad(loopId) ? ", bad" : "") << "): ";
        bool separator = false;
        for (const auto& curNode : curLoop) {
            ss << (separator ? " --> " : "") << graph.ToString(graph.Get(curNode));
            separator = true;
        }
        ss << Endl;
        out << ss.Str();

        if (Diag()->ShowBuildLoops && BuildLoops.contains(loopId)) {
            YErr(ShowBuildLoops) << ss.Str();
        } else if (Diag()->ShowDirLoops && DirLoops.contains(loopId)) {
            YErr(ShowDirLoops) << ss.Str();
        }
    }

    if (NYMake::TraceEnabled(ETraceEvent::L)) {
        loopNumber = 1;
        for (const auto& loopId : loopIds) {
            NEvent::TLoopDetected ev;
            ev.SetLoopId(loopNumber++);
            TGraphLoop curLoop = (*this)[AsIdx(loopId)];
            for (const auto& curNodeId : curLoop) {
                NEvent::TLoopItem& loopItem = *ev.AddLoopNodes();
                const auto curNode = graph.Get(curNodeId);
                loopItem.SetName(graph.ToString(curNode));
                loopItem.SetType(ToString(curNode->NodeType));
            }
            NYMake::Trace(ev);
        }
    }
}

struct TCollectBadNodesVisitorData {
    using TItemDebug = TVisitorStateItemBaseDebug;

    bool IsRemoved = false;
    bool InStack;

    TCollectBadNodesVisitorData(TItemDebug = {}, bool inStack = false)
        : InStack(inStack)
    {
    }
};

// Collects nodes that either belong to DirLoops or depend on them
class TCollectBadNodesVisitor: public TNoReentryVisitorBase<TCollectBadNodesVisitorData, TGraphIteratorStateItemBase<true>> {
public:
    using TBase = TNoReentryVisitorBase<TCollectBadNodesVisitorData, TGraphIteratorStateItemBase<true>>;

    const THashMap<TNodeId, TNodeId>& Node2Loop;
    const THashSet<TNodeId>& LoopsToRemove;

    THashSet<TNodeId> NodesToRemove;

public:
    TCollectBadNodesVisitor(const THashMap<TNodeId, TNodeId>& node2Loop, const THashSet<TNodeId>& loopsToRemove)
        : Node2Loop(node2Loop)
        , LoopsToRemove(loopsToRemove)
    {
    }

    bool AcceptDep(TState& state) {
        const auto& dep = state.NextDep();
        return IsLoopGenDep(dep) && TBase::AcceptDep(state);
    }

    bool Enter(TState& state) {
        bool fresh = TBase::Enter(state);

        if (fresh) {
            auto loopIt = Node2Loop.find(state.TopNode().Id());
            CurEnt->IsRemoved |= (loopIt != Node2Loop.end() && LoopsToRemove.contains(loopIt->second));
        }

        return fresh;
    }

    void Leave(TState& state) {
        TBase::Leave(state);
        if (CurEnt->IsRemoved) {
            NodesToRemove.insert(state.TopNode().Id());
        }
    }

    void Left(TState& state) {
        auto childEnt = CurEnt;
        TBase::Left(state);

        if (childEnt && childEnt->IsRemoved) {
            CurEnt->IsRemoved = true;
        }
    }
};

void TGraphLoops::RemoveBadLoops(TDepGraph& graph, TVector<TTarget>& startTargets) {
    auto nodesForRemove = GetNodesToRemove(graph, startTargets);

    // 1. Remove bad start targets
    TVector<TTarget> newStartTargets;
    newStartTargets.reserve(startTargets.size());
    for (const auto& target : startTargets) {
        if (!nodesForRemove.contains(target)) {
            newStartTargets.push_back(target);
        }
    }
    startTargets = std::move(newStartTargets);

    // 2. Remove bad nodes with connected edges and print error messages
    for (const auto& target : nodesForRemove) {
        const TDepGraph::TConstNodeRef node = graph.Get(target);
        if (node.IsValid() && IsModuleType(node.Value().NodeType)) {
            if (Diag()->ShowBuildLoops) {
                YErr(ShowBuildLoops) << fmt::format("the module {} not be built due to deprecated loop", graph.ToString(graph.Get(target))) << Endl;
            } else {
                YErr(ShowDirLoops) << fmt::format("the module {} not be built due to deprecated loop", graph.ToString(graph.Get(target))) << Endl;
            }
        }
        graph.DeleteNode(target);
    }
    graph.DeleteHangingEdges();
}

THashSet<TNodeId> TGraphLoops::GetNodesToRemove(TDepGraph& graph, TVector<TTarget>& startTargets) const {
    THashSet<TNodeId> loopsToRemove(DirLoops.begin(), DirLoops.end());
    loopsToRemove.insert(BuildLoops.begin(), BuildLoops.end());
    TCollectBadNodesVisitor collector(Node2Loop, loopsToRemove);
    IterateAll(graph, startTargets, collector, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
    return std::move(collector.NodesToRemove);
}
