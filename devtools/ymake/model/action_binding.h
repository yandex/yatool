#pragma once

#include <devtools/ymake/libs/polexpr/expression.h>

#include <util/generic/string.h>

// Compatibility boundary value: the producer has completed DSL parsing,
// inlining and evaluation, while the model still owns expression interning and
// binding storage. Lowered TSyntax can replace TExpression at this seam later.
struct TCompiledBindingExpression {
    NPolexpr::TExpression Expression;
};

// Metadata retained by the legacy global-variable spelling. The command ID
// and name are parsed by the producer; the model combines them with the
// canonical reference allocated while interning Value.
struct TCompiledGlobalBinding {
    ui64 CommandId;
    TString CommandName;
    TCompiledBindingExpression Value;
};
