#pragma once

#include "action_binding.h"
#include "action_context.h"
#include "action_glob.h"
#include "action_input.h"
#include "../macro_vars.h"

#include <util/generic/hash.h>
#include <util/generic/ptr.h>
#include <util/generic/vector.h>

#include <optional>

// Producer-complete compatibility data for one model commit.  Current
// TYVar/TVarStrEx representations remain here temporarily, but the model no
// longer reaches back into TCommandInfo for action state.
struct TActionSubmission {
    TResolvedActionInputs ResolvedInputs;
    TVector<std::optional<TEvaluatedActionGlob>> Globs;
    TVector<TPreparedGlobalBinding> GlobalBindings;

    TYVar Command;
    TSpecFileArr ActionInputs;
    TSpecFileArr Outputs;
    TSpecFileArr OutputIncludes;
    THashMap<TString, TSpecFileArr> OutputIncludesForType;
    THolder<TVars> LocalCommandBindings;
    THolder<TVars> GlobalCommandBindings;
    bool HasGlobalInput = false;

    TActionContinuation Continuation;
};

// Producer work that follows a successful model commit.  Output element IDs
// are assigned by the model, so this value is returned by SubmitAction rather
// than retained through a TCommandInfo pointer.
struct TActionCommitResult {
    bool HasCommand = false;
    TSpecFileArr Inputs;
    TSpecFileArr Outputs;
};

struct TVariableSubmission {
    TCmdElemId Variable;
    TResolvedActionInputs ResolvedInputs;
    TYVar Command;
    TSpecFileArr Inputs;
};
