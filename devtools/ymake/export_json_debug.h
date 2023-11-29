#pragma once

#include "node_debug.h"

namespace NDebugEvents::NExportJson {
    enum class EUidType {
        Cache,
        Render,
    };

    struct TCacheSearch {
        TNodeIdLog Node;
        EUidType UidType;
        TStringLogEntry Uid;
        bool Found;

        TCacheSearch() = default;
        TCacheSearch(const TDepGraph& graph, TNodeId node, EUidType uidType, TStringBuf uid, bool found);

        Y_SAVELOAD_DEFINE(Node, UidType, Uid, Found);
    };
}
