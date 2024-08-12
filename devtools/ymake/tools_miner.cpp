#include "tools_miner.h"

#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/query.h>

#include <util/generic/stack.h>

namespace {
    struct TStackFrame {
        TConstDepRef Dep;
        TConstDepNodeRef::TIterator First;
        TConstDepNodeRef::TIterator Last;
        TUniqVector<ui32> ChildTools;
        TUniqVector<ui32>& ParentTools;
    };
}

TVector<ui32> TToolMiner::MineTools(TConstDepNodeRef genFileNode) {
    TUniqVector<ui32> tools;
    TStack<TStackFrame> stack;
    for (TConstDepRef dep : genFileNode.Edges()) {
        if (!IsBuildCommandDep(dep)) {
            continue;
        }
        stack.push({
            .Dep = dep,
            .First = MinedCache.contains(dep.To().Id()) ? dep.To().Edges().end() : dep.To().Edges().begin(),
            .Last = dep.To().Edges().end(),
            .ChildTools = {},
            .ParentTools = tools
        });
        while (!stack.empty()) {
            if (stack.top().First == stack.top().Last) {
                auto& frame = stack.top();
                if (IsDirectToolDep(frame.Dep)) {
                    frame.ParentTools.Push(frame.Dep.To()->ElemId);
                } else {
                    const auto [pos, inserted] = MinedCache.emplace(frame.Dep.To().Id(), frame.ChildTools.Take());
                    for (ui32 elemId: pos->second) {
                        frame.ParentTools.Push(elemId);
                    }
                }
                stack.pop();
                continue;
            }
            stack.top().First = std::find_if(stack.top().First, stack.top().Last, [](const auto& edge) {
                return IsInnerCommandDep(edge) || IsDirectToolDep(edge);
            });
            if (stack.top().First != stack.top().Last) {
                const TConstDepRef next = *stack.top().First++;
                stack.push({
                    .Dep = next,
                    .First = MinedCache.contains(next.To().Id()) ? next.To().Edges().end() : next.To().Edges().begin(),
                    .Last = next.To().Edges().end(),
                    .ChildTools = {},
                    .ParentTools = stack.top().ChildTools
                });
            }
        }
    }
    return tools.Take();
}
