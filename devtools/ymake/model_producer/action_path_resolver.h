#pragma once

#include "../model/action_input.h"

struct TCommandInfo;
class TModuleBuilder;

enum class EActionInputResolution {
    Ready,
    Pending,
    Skipped,
};

// Producer-side input resolution for a prepared action.
class TActionInputResolver {
public:
    EActionInputResolution Resolve(
        TCommandInfo& commandInfo,
        TModuleBuilder& moduleBuilder,
        IActionInputModelSink& modelSink,
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
