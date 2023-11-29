#pragma once

#include "misc.h"
#include "node.h"
#include "module.h"

#include <devtools/ymake/compact_graph/dep_graph.h>

#include <util/system/types.h>

namespace NYMake {
    namespace NMsvs {
        using TFileFlagsMap = TModule::TFileFlagsMap;
        using TFileOutSuffixMap = TModule::TFileOutSuffixMap;

        void GetCommandOutputs(const TConstDepNodeRef& cmd, TNodeIds& result);
        void GetTargetPeers(const TConstDepNodeRef& target, TNodeIds& result);
        void GetTargetGlobalObjects(const TConstDepNodeRef& target, TNodeIds& result);
        void CollectModuleSrcs(const TConstDepNodeRef& mod, THashSet<TNodeId>& fnodes, THashSet<TNodeId>* fnodesWithBuildCmd, THashSet<TNodeId>* fnodesGlobal,
                               TFileFlagsMap& fileFlags, TFileOutSuffixMap& fileOutSuffix, bool skipGlobals);
    }
}
