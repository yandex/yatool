#pragma once

#include "../model/action_binding.h"

#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

struct TCommandInfo;
class TModuleBuilder;

TCompiledBindingExpression CompileConfigurationBinding(
    TCommandInfo& commandInfo,
    const TVector<TStringBuf>& variableNames
);

TVector<TString> CollectGlobalBindingNames(const TModuleBuilder& moduleBuilder);

TMaybe<TCompiledGlobalBinding> CompileGlobalBinding(
    TModuleBuilder& moduleBuilder,
    TStringBuf variableName
);
