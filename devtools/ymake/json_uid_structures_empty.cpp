#include "json_uid_structures_empty.h"

TJsonMD5NewUID::TJsonMD5NewUID(TNodeDebugOnly)
{}

TJSONEntryStatsNewUID::TJSONEntryStatsNewUID(TNodeDebugOnly nodeDebug, bool inStack, bool isFile)
    : TEntryStats(nodeDebug, inStack, isFile)
{}
