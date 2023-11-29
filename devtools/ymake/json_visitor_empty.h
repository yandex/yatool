#pragma once

#include "json_entry_stats.h"

#include "managed_deps_iter.h"

#include <devtools/ymake/compact_graph/loops.h>

struct TRestoreContext;
class TCommands;

class TJSONVisitorNew: public TManagedPeerVisitor<TJSONEntryStats, TJsonStateItem> {
protected:
    TGraphLoops Loops;

public:
    TJSONVisitorNew(const TRestoreContext& restoreContext, TCommands& commands, const TVector<TTarget>& startDirs);

    using TBase = TManagedPeerVisitor<TJSONEntryStats, TJsonStateItem>;
    using TBase::Nodes;
    using typename TBase::TState;
};
