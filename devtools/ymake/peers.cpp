#include "peers.h"

#include "module_state.h"
#include "module_store.h"

#include <devtools/ymake/compact_graph/query.h>

EPeerSearchStatus TPeersRestrictions::Match(const TModule& childModule, const TMatchPeerRequest& matchRequest) const {
    for (const auto& restriction : Restrictions) {
        bool useRestriction = matchRequest.UseRules != matchRequest.Exceptions.contains(restriction.StatusOnMatch);

        if (useRestriction && restriction.Match(childModule)) {
            return restriction.StatusOnMatch;
        }
    }

    return EPeerSearchStatus::Match;
}

TGetPeerNodeResult NPeers::GetPeerNode(const TModules& modules, const TConstDepNodeRef& dirNode, const TModule* srcMod, TMatchPeerRequest&& request) {
    EPeerSearchStatus statusAccumulator = EPeerSearchStatus::NoModules;
    bool matchTags = request.UseRules != request.Exceptions.contains(EPeerSearchStatus::DeprecatedByTags);
    bool matchTagsFinalized = false; // multimodule property must be before modules

    for (auto dep : dirNode.Edges()) {
        if (*dep == EDT_Property && !matchTags) {
            auto modNode = dep.To();
            if (modNode->NodeType == EMNT_Property && GetPropertyName(TDepGraph::GetCmdName(modNode).GetStr()) == NProps::MULTIMODULE) {
                Y_ASSERT(!matchTagsFinalized);

                if (request.UseRules) {
                    request.Exceptions.erase(EPeerSearchStatus::DeprecatedByTags);
                } else {
                    request.Exceptions.insert(EPeerSearchStatus::DeprecatedByTags);
                }
                matchTags = true;

//                Y_ASSERT(srcMod != nullptr); // We need Source Module to steer selection in this case
            }
        } else if (*dep == EDT_Include) {
            auto modNode = dep.To();

            if (IsModuleType(modNode->NodeType)) {
                matchTagsFinalized = true;

                const TModule* sinkMod = modules.Get(modNode->ElemId);
                Y_ASSERT(sinkMod != nullptr);

                // For tooldirs
                if (request.FindAnyFinalTarget) {
                    if (sinkMod->IsFinalTarget()) {
                        return TGetPeerNodeResult(modNode, EPeerSearchStatus::Match);
                    } else {
                        statusAccumulator = std::min(statusAccumulator, EPeerSearchStatus::DeprecatedByFilter);
                        continue;
                    }
                }

                if ((!request.UseRules && request.Exceptions.empty())) { // no checks required
                    return TGetPeerNodeResult(modNode, EPeerSearchStatus::Match);
                }

                Y_ASSERT(srcMod != nullptr);

                EPeerSearchStatus status = srcMod->MatchPeer(*sinkMod, request);
                switch (status) {
                    case EPeerSearchStatus::Match:
                        return TGetPeerNodeResult(modNode, status);
                    case EPeerSearchStatus::Error:
                        return TGetPeerNodeResult(TDepGraph::GetInvalidNode(dirNode), status);
                    default:
                        statusAccumulator = std::min(statusAccumulator, status);
                }
            }
        }
    }

    return TGetPeerNodeResult(TDepGraph::GetInvalidNode(dirNode), statusAccumulator);
}

TConstDepNodeRef NPeers::GetDirectPeerNode(const TDepGraph& graph, const TModules& modules, const TConstDepNodeRef& from, TNodeId dirId, bool isTooldir) {
    for (const auto& edge : from.Edges()) {
        const auto childNode = edge.To();

        auto isCorrectDepType = isTooldir ? IsDirectToolDep(edge) : IsDirectPeerdirDep(edge);
        if (childNode.IsValid() && isCorrectDepType) {
            const auto module = modules.Get(childNode->ElemId);
            if (module != nullptr && graph.GetFileNode(module->GetDir()).Id() == dirId) {
                return childNode;
            }
        }
    }

    return graph.GetInvalidNode();
}
