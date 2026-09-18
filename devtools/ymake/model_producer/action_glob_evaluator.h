#pragma once

#include "../model/action_glob.h"

#include <optional>

// Producer-side pattern evaluation for glob inputs retained by an action.
class TActionGlobEvaluator {
public:
    explicit TActionGlobEvaluator(TActionGlobEvaluationContext context);

    std::optional<TEvaluatedActionGlob> Evaluate(TStringBuf pattern) const;

private:
    TActionGlobEvaluationContext Context_;
};
