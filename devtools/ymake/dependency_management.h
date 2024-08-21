#pragma once

#include <util/generic/vector.h>
#include <util/generic/hash.h>

#include <devtools/ymake/compact_graph/graph.h>
#include <devtools/ymake/vars.h>

struct TRestoreContext;
struct TTarget;

enum class EManagedPeersDepth {
    Direct,
    Transitive
};

struct TDependencyManagementModuleInfo {
    TVector<TNodeId> AppliedExcludes;
};

void ExplainDM(TRestoreContext restoreContext, const THashSet<TNodeId>& roots);
void DumpDM(TRestoreContext restoreContext, const THashSet<TNodeId>& roots, EManagedPeersDepth depth = EManagedPeersDepth::Transitive);
void DumpFDM(const TVars& globalVars, bool asJson);

// Exposed for unit tests
namespace NDetail {
    struct TClosureStats {
        TClosureStats(ui32 pathCount) noexcept: Excluded{false}, PathCount{pathCount} {}

        union {
            ui32 AllBits = 0;
            struct {
                ui32 Excluded:   1;
                ui32 PathCount: 31;
            };
        };
    };

    class TPeersClosure {
    public:
        using TStatItem = THashMap<TNodeId, TClosureStats>::value_type;

        TPeersClosure() = default;

        TPeersClosure(const TPeersClosure&) = delete;
        TPeersClosure& operator=(const TPeersClosure&) = delete;

        TPeersClosure(TPeersClosure&&) = default;
        TPeersClosure& operator=(TPeersClosure&&) = default;

        void Merge(TNodeId node, const TPeersClosure& closure, ui32 pathsCount = 1);
        TPeersClosure Exclude(const std::function<bool(TNodeId)>& predicate, const std::function<const TPeersClosure&(TNodeId)>& nodeClosure) const;

        const THashMap<TNodeId, TClosureStats>& GetStats() const noexcept {
            return Stats;
        }

        const TVector<TStatItem*>& GetStableOrderStats() const noexcept {
            return TopSort;
        }

        bool Contains(TNodeId id) const noexcept;
        bool ContainsWithAnyStatus(TNodeId id) const noexcept;

    private:

        THashMap<TNodeId, TClosureStats> Stats;
        TVector<TStatItem*> TopSort;
    };
}
