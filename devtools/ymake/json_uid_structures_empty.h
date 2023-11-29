#pragma once

#include "node_debug.h"

#include <devtools/ymake/compact_graph/iter.h>

struct TRestoreContext;

class TJsonMD5NewUID {
public:
    TJsonMD5NewUID(TNodeDebugOnly);
};

struct TJSONEntryStatsNewUID : public TEntryStats {
    TJSONEntryStatsNewUID(TNodeDebugOnly nodeDebug, bool inStack, bool isFile);
};
