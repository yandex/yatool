#pragma once

#include "module_restorer.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/iter.h>
#include <devtools/ymake/json_entry_stats.h>

class TGlobalVarsCollector {
private:
    TRestoreContext RestoreContext;
public:
    using TStateItem = TGraphIteratorStateItemBase<true>;
    TGlobalVarsCollector(const TRestoreContext& restoreContext)
        : RestoreContext{restoreContext} {
    }

    bool Start(const TStateItem& parentItem);
    void Finish(const TStateItem& parentItem, TEntryStatsData* parentData);
    void Finish(const TStateItem& parentItem, TJSONEntryStats* parentData);
    void Collect(const TStateItem& parentItem, TConstDepNodeRef peerNode);
};
