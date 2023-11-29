#pragma once

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/compact_graph/dep_graph.h>

class TModule;
class TModules;

// Can be one of the following:
// a) Result of a direct peerdir validation ((module, module) -> EPeerSearchStatus)
// b) Result of search suitable for peerdir module in a directory ((module, dir) -> (moduleNode, EPeerSearchStatus))
// To map {EPeerSearchStatus} -> EPeerSearchStatus take status with minimum priority value
enum class EPeerSearchStatus {
    Match                = 1,
    NoModules            = 6,
    DeprecatedByTags     = 4,
    DeprecatedByRules    = 2,
    DeprecatedByInternal = 3,
    DeprecatedByFilter   = 5,
    Error                = 0,
    Unknown              = 7
};

// Which rules to use
struct TMatchPeerRequest {
    bool UseRules = false;
    bool FindAnyFinalTarget = false;

    THashSet<EPeerSearchStatus> Exceptions;

    static TMatchPeerRequest CheckAll() {
        return {true, false, {}};
    }
    static TMatchPeerRequest CheckNothing() {
        return {false, false, {}};
    }
    static TMatchPeerRequest CheckOnly(std::initializer_list<EPeerSearchStatus> statuses) {
        return {false, false, {statuses}};
    }
};

// Describes one type of checks: tags matching, filter with UsePeers, check with rules from peerdirs.policy etc
struct TPeerRestriction {
    using TMatchOperator = std::function<bool(const TModule&)>;

    EPeerSearchStatus StatusOnMatch;
    TMatchOperator Match;

    TPeerRestriction(EPeerSearchStatus status, TMatchOperator&& matcher)
        : StatusOnMatch(status)
        , Match(std::move(matcher))
    {}
};

// Belongs to a parent TModule
class TPeersRestrictions {
private:
    TVector<TPeerRestriction> Restrictions;

public:
    void Add(TPeerRestriction&& restriction) {
        Restrictions.push_back(std::move(restriction));
    }

    EPeerSearchStatus Match(const TModule& childModule, const TMatchPeerRequest& matchRequest) const;
};

struct TGetPeerNodeResult {
    TConstDepNodeRef Node;
    EPeerSearchStatus Status;

    TGetPeerNodeResult(TConstDepNodeRef node, EPeerSearchStatus status)
        : Node(node)
        , Status(status)
    {}
};

namespace NPeers {
    TGetPeerNodeResult GetPeerNode(const TModules& modules, const TConstDepNodeRef& dirNode, const TModule* srcMod, TMatchPeerRequest&& request);

    TConstDepNodeRef GetDirectPeerNode(const TDepGraph& graph, const TModules& modules, const TConstDepNodeRef& from, TNodeId dirId, bool isTooldir = false);
}
