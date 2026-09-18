#pragma once

#include "../model/action_binding.h"
#include "../model/action_context.h"

#include <util/generic/maybe.h>
#include <util/generic/string.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>

class TModuleBuilder;

TCompiledBindingExpression CompileConfigurationBinding(
    const TActionModelContext& context,
    const TVector<TStringBuf>& variableNames
);

TVector<TPreparedGlobalBinding> CompileGlobalBindings(TModuleBuilder& moduleBuilder);
