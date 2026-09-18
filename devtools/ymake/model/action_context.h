#pragma once

#include <util/generic/string.h>
#include <util/generic/vector.h>

class TBuildConfiguration;
class TDepGraph;
class TModule;
class TUpdIter;

// Non-owning model context retained by action continuations.  Keeping this
// separate from TCommandInfo prevents producer working state from leaking into
// deferred model operations.
struct TActionModelContext {
    const TBuildConfiguration* Conf = nullptr;
    TDepGraph* Graph = nullptr;
    TUpdIter* UpdIter = nullptr;
    TModule* Module = nullptr;
};

// The only command resources needed after action submission are tools/results
// originating in a nested local variable.  GeneralParser attaches their
// directory dependencies when it later visits the command node.
struct TDeferredCommandResource {
    TString Name;
    bool IsResult = false;
};

struct TActionContinuation {
    TActionModelContext Model;
    TVector<TDeferredCommandResource> LocalResources;
};
