#include "loops.h"

#include "iter.h"
#include "nodes_data.h"
#include "query.h"

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <util/generic/hash_set.h>
#include <util/stream/format.h>

#include <fmt/format.h>

namespace {

struct TLoopId {
    TNodeId LoopId = TNodeId::Invalid;
    bool IsFile = false;
};

struct TCollectedLoops: TMoveOnly {
    THashMap<TNodeId, TNodeId> Node2Loop;
    TNodesData<TGraphLoop, TVector> Loop2Nodes;
};

using TLoopData = TVisitorStateItem<TLoopId>;

class TLoopSearcher: public TNoReentryStatsVisitor<TLoopData, TGraphConstIteratorStateItemBase> {
public:
    using TBase = TNoReentryStatsVisitor<TLoopData, TGraphConstIteratorStateItemBase>;

    // Disjoint-set with path compression and union by rank
    class TGluedLoops {
    private:
        struct TLoopData {
            TLoopData() noexcept = default;

            TNodeId ParentLoop = TNodeId::Invalid;
            size_t Rank = 0;
        };

        TNodesData<TLoopData, TVector> Loops;

    public:
        TGluedLoops()
            : Loops{}
        {}

        TNodeId AddLoop() {
            return Loops.emplace_back();
        }

        TNodeId GetLoopId(TNodeId loopId) {
            TVector<TNodeId> pathToRoot;

            while (Loops[loopId].ParentLoop > TNodeId::Invalid) {
                pathToRoot.push_back(loopId);
                loopId = Loops[loopId].ParentLoop;
            }

            for (const auto& loopNum : pathToRoot) {
                Loops[loopNum].ParentLoop = loopId;
            }

            return loopId;
        }

        void JoinLoops(TNodeId first, TNodeId second) {
            first = GetLoopId(first);
            second = GetLoopId(second);
            if (first == second) {
                return;
            }

            if (Loops[first].Rank < Loops[second].Rank) {
                std::swap(first, second);
            } else if (Loops[first].Rank == Loops[second].Rank) {
                Loops[first].Rank++;
            }
            Loops[second].ParentLoop = first;
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

    TCollectedLoops CollectLoops() {
        TVector<std::pair<TNodeId, TNodeId>> loopSrt;
        for (const auto& node : Nodes) {
            TNodeId loopId = node.second.LoopId;
            if (loopId != TNodeId::Invalid) {
                loopId = GluedLoops.GetLoopId(loopId);
                loopSrt.push_back(std::make_pair(loopId, node.first));
            }
        }
        if (loopSrt.empty()) {
            return {};
        }
        std::sort(loopSrt.begin(), loopSrt.end());
        // continuous loop enumeration makes other code simpler
        TNodesData<size_t, TVector> loopSz;
        TNodeId curId = loopSrt[0].first;
        loopSz.push_back(0);
        for (size_t n = 0; n < loopSrt.size(); n++) {
            if (curId != loopSrt[n].first) {
                loopSz.push_back(0);
                curId = loopSrt[n].first;
            }
            loopSz.back()++;
        }
        TCollectedLoops res{
            .Node2Loop{},
            .Loop2Nodes{loopSz.Ids()}
        };
        // loop id 0 is reserved for 'no loop' flag
        auto newId = res.Loop2Nodes.ValidIds().begin();
        curId = loopSrt[0].first;
        for (size_t n = 0; n < loopSrt.size(); n++) {
            if (curId != loopSrt[n].first) {
                ++newId;
                res.Loop2Nodes[*newId].reserve(loopSz[*newId]);
                curId = loopSrt[n].first;
            }
            res.Node2Loop[loopSrt[n].second] = *newId;
            res.Loop2Nodes[*newId].push_back(loopSrt[n].second);
        }
        Y_ASSERT(newId == loopSz.MaxNodeId());
        YDIAG(Loop) << "Found " << *newId << " loops, with " << loopSrt.size() << " elements (of " << Nodes.size() << " total nodes)" << Endl;
        return res;
    }
};

}

bool TGraphLoops::HasBadLoops() const {
    return !DirLoops.empty() || !BuildLoops.empty();
}

TGraphLoops TGraphLoops::Find(const TDepGraph& graph, const TVector<TTarget>& startTargets, bool outTogetherIsLoop) {
    TLoopSearcher ls(outTogetherIsLoop);
    IterateAll(graph, startTargets, ls, [](const TTarget& t) -> bool { return t.IsModuleTarget; });
    auto collected = ls.CollectLoops();

    THashSet<TNodeId> dirLoops;
    THashSet<TNodeId> buildLoops;
    for (TNodeId loopNum: collected.Loop2Nodes.ValidIds()) {
        const TGraphLoop& curLoop = collected.Loop2Nodes[loopNum];

        bool isDirLoop = false;
        bool isBuildLoop = false;

        THashSet<TNodeId> nodesInLoop(curLoop.begin(), curLoop.end());

        for (const auto& curNode : curLoop) {
            const auto& node = graph.Get(curNode);

            if (!isDirLoop && node->NodeType == EMNT_Directory) {
                dirLoops.insert(loopNum);
                isDirLoop = true;
            }

            if (!isBuildLoop) {
                for (const auto& edge : node.Edges()) {
                    if (*edge == EDT_BuildFrom && nodesInLoop.contains(edge.To().Id())) {
                        buildLoops.insert(loopNum);
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

    return TGraphLoops{std::move(collected.Loop2Nodes), std::move(dirLoops), std::move(buildLoops), std::move(collected.Node2Loop)};
}

void TGraphLoops::DumpAllLoops(const TDepGraph& graph, IOutputStream& out) const {
    if (ValidIds().size() != 0) {
        DumpLoops(graph, out, ValidIds());
    } else {
        out << "Loops were not detected" << Endl;
    }
}

void TGraphLoops::DumpDirLoops(const TDepGraph& graph, IOutputStream& out) const {
    DumpLoops(graph, out, DirLoops);
}

void TGraphLoops::DumpBuildLoops(const TDepGraph& graph, IOutputStream& out) const {
    DumpLoops(graph, out, BuildLoops);
}

template <typename TContainer>
void TGraphLoops::DumpLoops(const TDepGraph& graph, IOutputStream& out, const TContainer& loopIds) const {
    const auto& isLoopBad = [this](TNodeId loopId) {
        return DirLoops.contains(loopId) || BuildLoops.contains(loopId);
    };

    size_t loopNumber = 1;
    for (const auto& loopId : loopIds) {
        const TGraphLoop& curLoop = (*this)[loopId];

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
            YConfErr(ShowBuildLoops) << ss.Str();
        } else if (Diag()->ShowDirLoops && DirLoops.contains(loopId)) {
            YConfErr(ShowDirLoops) << ss.Str();
        }
    }

    if (NYMake::TraceEnabled(ETraceEvent::L)) {
        loopNumber = 1;
        for (const auto& loopId : loopIds) {
            NEvent::TLoopDetected ev;
            ev.SetLoopId(loopNumber++);
            TGraphLoop curLoop = (*this)[loopId];
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

void TGraphLoops::RemoveBadLoops(TDepGraph& graph, TVector<TTarget>& startTargets) const {
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
    constexpr static const char errorMessageTemplate[] = "the module {} will not be built due to deprecated loop";
    for (const auto& target : nodesForRemove) {
        const TDepGraph::TConstNodeRef node = graph.Get(target);
        if (node.IsValid() && IsModuleType(node.Value().NodeType)) {
            if (Diag()->ShowBuildLoops) {
                YConfErr(ShowBuildLoops) << fmt::format(errorMessageTemplate, graph.ToString(graph.Get(target))) << Endl;
            } else {
                YConfErr(ShowDirLoops) << fmt::format(errorMessageTemplate, graph.ToString(graph.Get(target))) << Endl;
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
