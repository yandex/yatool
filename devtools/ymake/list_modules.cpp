#include "ymake.h"

#include "module_store.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>

#include <util/generic/vector.h>
#include <util/system/types.h>

namespace {
    bool IsFakeLib(const TModules& mods, ui32 elemId) {
        return mods.Get(elemId)->IsFakeModule();
    }
    bool IsCompleteTarget(const TModules& mods, ui32 elemId) {
        return mods.Get(elemId)->IsCompleteTarget();
    }
}

void TYMake::ListTargetResults(const TTarget& startTarget, TVector<TNodeId>& modules, TVector<TNodeId>& globSrcs) const {
    TNodeId nodeStart = startTarget.Id;
    auto node = Graph[nodeStart];
    if (!IsModuleType(node->NodeType)) {
        if (IsFileType(node->NodeType)) {
            modules.push_back(nodeStart);
        }
        return;
    }

    if (!IsFakeLib(Modules, node->ElemId)) {
        modules.push_back(node.Id());
    }

    if (node->NodeType == EMNT_Library && !IsCompleteTarget(Modules, node->ElemId)) {
        for (auto xdep: node.Edges()) {
            if (IsGlobalSrcDep(xdep) && xdep.To()->NodeType == EMNT_NonParsedFile) {
                globSrcs.push_back(xdep.To().Id());
            }
        }
    }

    const TModule* mod = Modules.Get(node->ElemId);
    Y_ASSERT(mod != nullptr);
    ui32 dirId = mod->GetDirId();
    if (startTarget.IsDependsTarget) {
        const auto dirName = Graph.GetFileName(dirId).CutType();
        auto iter = DependsToModulesClosure.find(dirName);
        if (iter != DependsToModulesClosure.end()) {
            modules.insert(modules.end(), iter->second.begin(), iter->second.end());
        }
    }
}
