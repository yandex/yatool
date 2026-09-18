#pragma once

#include "../model/action_input.h"

struct TCommandInfo;
class TModuleBuilder;

enum class EActionInputResolution {
    Ready,
    Pending,
    Skipped,
};

struct TActionInputResolutionResult {
    EActionInputResolution State;
    TResolvedActionInputs Inputs;
};

// Producer-side input resolution for an action draft.
class TActionInputResolver {
public:
    TActionInputResolutionResult Resolve(
        TCommandInfo& commandInfo,
        TModuleBuilder& moduleBuilder,
        bool lastTry
    ) const;
};

// Producer-side output path preparation after inputs are ready and before the
// action is submitted to the graph model.
class TActionOutputResolver {
public:
    bool Resolve(
        TCommandInfo& commandInfo,
        TModuleBuilder& moduleBuilder
    ) const;
};
