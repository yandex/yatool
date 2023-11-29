#include "export_json_debug.h"

namespace NDebugEvents::NExportJson {
    TCacheSearch::TCacheSearch(const TDepGraph& graph, TNodeId node, EUidType uidType, TStringBuf uid, bool found)
        : Node(graph, node)
        , UidType(uidType)
        , Uid(TString{uid})
        , Found(found)
    {
    }
}
