#pragma once

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <util/generic/hash.h>

class TBuildConfiguration;
struct TVars;
class TModules;

void MineVariables(
    const TBuildConfiguration& conf,
    const TConstDepNodeRef& node,
    THolder<THashMap<TString, TString>>& toolPaths,
    THolder<THashMap<TString, TString>>& resultPaths,
    TVars& vars,
    TUniqVector<TNodeId>& lateOutsProps,
    const TModules& modules);

void MineVariables(
    const TConstDepNodeRef& node,
    TVars& vars);
