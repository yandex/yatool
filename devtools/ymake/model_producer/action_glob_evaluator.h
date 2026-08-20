#pragma once

#include "../model/action_glob.h"

// Producer-side pattern evaluation for glob inputs retained by an action.
class TActionGlobEvaluator final : public IActionGlobEvaluator {
public:
    explicit TActionGlobEvaluator(TActionGlobEvaluationContext context);

    TEvaluatedActionGlob Evaluate(TStringBuf pattern) override;
    void ReportInvalidPattern(TStringBuf pattern, TStringBuf error) override;

private:
    TActionGlobEvaluationContext Context_;
};
