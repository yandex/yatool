#include "mine_variables.h"

#include "conf.h"
#include "macro_string.h"
#include "module_store.h"
#include "prop_names.h"
#include "vars.h"

#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/compact_graph/query.h>

#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/string.h>
#include <util/stream/output.h>
#include <util/system/types.h>
#include <util/system/yassert.h>


static inline TString RealPath(const TConstDepNodeRef& node, const TBuildConfiguration& conf) {
    return conf.RealPath(TDepGraph::GetFileName(node));
}

TString MineResults(const TModules& modules, const TBuildConfiguration& conf, TFileView name, const TConstDepNodeRef& node) {
    Y_ASSERT(IsDirType(node->NodeType));

    for (auto dep : node.Edges()) {
        TConstDepNodeRef depNode = dep.To();
        if (IsModuleType(depNode->NodeType) && *dep == EDT_Include) {
            const TModule* module = modules.Get(depNode->ElemId);
            Y_ASSERT(module != nullptr);
            if (module->IsFinalTarget()) {
                return RealPath(depNode, conf);
            }
        }
    }

    return TString(name.GetTargetStr());
}

namespace {
    template<typename FDescend>
    inline void MineThisVariable(const TConstDepNodeRef& nextNode, TVars& vars, bool structCmd, FDescend descend) {
        TStringBuf name = TDepGraph::GetCmdName(nextNode).GetStr();
        TStringBuf cmdName, cmd;
        ui64 id;
        ParseLegacyCommandOrSubst(name, id, cmdName, cmd);
        TVars& lvars = id ? vars : *const_cast<TVars*>(vars.Base); // hack for SET_APPEND
        auto [lvarsIt, lvarsAdded] = lvars.try_emplace(cmdName);
        if (!lvarsAdded) {                                              // overridden (these always come in first)
            return;
        }
        TYVar& var = lvarsIt->second;
        descend();
        var.push_back(TVarStr(name, true, false));
        var.BaseVal = lvars.Base ? lvars.Base->Lookup(cmdName) : nullptr;
        var.back().NeedSubst = true;
        var.back().StructCmdForVars = structCmd;
    }
}

// MineVariables sets toolPaths[toolDir] = toolPath for each tool reachable from the node.
// toolDir example: contrib/tools/ragel6
// toolPath example: $(BUILD_ROOT)/contrib/tools/ragel6/ragel6
void MineVariables(
    const TBuildConfiguration& conf,
    const TConstDepNodeRef& node,
    THolder<THashMap<TString, TString>>& toolPaths,
    THolder<THashMap<TString, TString>>& resultPaths,
    TVars& vars,
    TUniqVector<TNodeId>& lateOutsProps,
    const TModules& modules
) {
    const TDepGraph& graph = TDepGraph::Graph(node);
    YDIAG(MkCmd) << "CM<- " << TDepGraph::ToString(node) << Endl;
    size_t cnt = 0;
    size_t numDeps = node.Edges().Total();

    for (auto dep : node.Edges()) {
        TConstDepNodeRef nextNode = dep.To();
        YDIAG(MkCmd) << cnt++ << " dep from " << numDeps << " of " << TDepGraph::ToString(node) << ": " << TDepGraph::ToString(nextNode) << Endl;

        auto nodeType = nextNode->NodeType;
        auto depType = *dep;
        if (IsDirectToolDep(dep)) {
            const auto* tool = modules.Get(dep.To()->ElemId);
            Y_ASSERT(tool);
            const auto toolPath = RealPath(dep.To(), conf);
            GetOrInit(toolPaths)[tool->GetDir().CutType()] = toolPath;
            YDIAG(MkCmd) << "*Tool for " << TDepGraph::ToString(node) << ": " << toolPath << "\n";
            continue;
        }
        if (nodeType == EMNT_UnknownCommand || IsFileType(nodeType)) {
            continue;
        }

        if (depType == EDT_Property) {
            bool isLateOutProperty = GetPropertyName(graph.GetCmdName(dep.To()).GetStr()) == NProps::LATE_OUT;
            if (isLateOutProperty) {
                lateOutsProps.Push(dep.To().Id());
            }
            continue;
        }

        if (depType == EDT_BuildCommand) {
            continue;
        }

        if (depType == EDT_BuildFrom || depType == EDT_Include) {
            if (IsDirType(nodeType)) {
                TFileView name = TDepGraph::GetFileName(nextNode);
                TStringBuf toolDir = name.CutType();
                TString resultPath = MineResults(modules, conf, name, nextNode);
                GetOrInit(resultPaths)[toolDir] = resultPath;
                YDIAG(MkCmd) << "*Result for " << TDepGraph::ToString(node) << ": " << resultPath << "\n";
                continue;
            }
            MineThisVariable(nextNode, vars, false, [&]() {
                MineVariables(conf, nextNode, toolPaths, resultPaths, vars, lateOutsProps, modules);
            });
        }
    }
}

// a miner variation that collects just variables from just that single node
void MineVariables(
    const TConstDepNodeRef& node,
    TVars& vars
) {
    YDIAG(MkCmd) << "CMS<- " << TDepGraph::ToString(node) << Endl;
    size_t cnt = 0;
    size_t numDeps = node.Edges().Total();

    for (auto dep : node.Edges()) {
        TConstDepNodeRef nextNode = dep.To();
        YDIAG(MkCmd) << cnt++ << " dep from " << numDeps << " of " << TDepGraph::ToString(node) << ": " << TDepGraph::ToString(nextNode) << Endl;

        auto nodeType = nextNode->NodeType;
        auto depType = *dep;

        if (
            (depType == EDT_Include && nodeType == EMNT_BuildCommand) ||
            (depType == EDT_BuildCommand && nodeType == EMNT_BuildVariable)
        )
            MineThisVariable(nextNode, vars, true, [&]() {});
    }
}
