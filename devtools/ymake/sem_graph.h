#pragma once
#include "module_restorer.h"

#include <devtools/ymake/compact_graph/iter_starts_ctx.h>

class IOutputStream;
class TCommands;

void RenderSemGraph(
    IOutputStream& out,
    TRestoreContext restoreContext,
    const TCommands& commands,
    TTraverseStartsContext startsContext
);
