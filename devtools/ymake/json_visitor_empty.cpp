#include "json_visitor_empty.h"

#include "module_restorer.h"

TJSONVisitorNew::TJSONVisitorNew(const TRestoreContext& restoreContext, TCommands&, const TVector<TTarget>& startDirs)
    : TBase{restoreContext, TDependencyFilter{TDependencyFilter::SkipRecurses}}
{
    Loops.FindLoops(RestoreContext.Graph, startDirs, false);
}
