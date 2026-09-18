#include "action_binding_builder.h"

#include "../add_iter.h"
#include "../command_store.h"
#include "../macro_string.h"
#include "../module_builder.h"
#include "../ymake.h"

#include <devtools/ymake/diag/diag.h>

#include <util/stream/str.h>

TCompiledBindingExpression CompileConfigurationBinding(
    const TActionModelContext& context,
    const TVector<TStringBuf>& variableNames
) {
    TStringStream cfgVars;
    for (const auto variableName : variableNames) {
        cfgVars << " " << variableName << "=$" << variableName;
    }

    YDIAG(VV) << "CFG_VARS [" << context.Module->Vars.Id << "] -> " << cfgVars.Str() << Endl;
    auto compiled = context.UpdIter->YMake.Commands.Compile(
        cfgVars.Str(),
        *context.Conf,
        context.Module->Vars,
        false,
        {}
    );
    return {.Expression = std::move(compiled.Expression)};
}

namespace {
TVector<TString> CollectGlobalBindingNames(const TModuleBuilder& moduleBuilder) {
    TVector<TString> names;
    names.reserve(
        moduleBuilder.GetModuleConf().Globals.size() +
        moduleBuilder.GetModule().ExternalResources.size()
    );
    for (const auto& variable : moduleBuilder.GetModuleConf().Globals) {
        names.push_back(TString::Join(variable, "_GLOBAL"));
    }
    for (const auto& resource : moduleBuilder.GetModule().ExternalResources) {
        names.push_back(resource);
    }
    return names;
}

TMaybe<TCompiledGlobalBinding> CompileGlobalBinding(
    TCommands& commands,
    TVars& variables,
    const TBuildConfiguration& conf,
    TStringBuf variableName
) {
    const TYVar* variable = variables.Lookup(variableName);
    if (!variable) {
        return Nothing();
    }

    const TStringBuf variableText = Get1(variable);
    if (variableText.empty() || GetCmdValue(variableText).empty()) {
        return Nothing();
    }

    ui64 commandId;
    TStringBuf commandName;
    TStringBuf commandValue;
    ParseCommandLikeVariable(variableText, commandId, commandName, commandValue);

    auto compiled = commands.Compile(
        commandValue,
        conf,
        variables,
        false,
        {}
    );
    return TCompiledGlobalBinding{
        .CommandId = commandId,
        .CommandName = TString{commandName},
        .Value = {.Expression = std::move(compiled.Expression)},
    };
}
}

TVector<TPreparedGlobalBinding> CompileGlobalBindings(TModuleBuilder& moduleBuilder) {
    TVector<TPreparedGlobalBinding> result;
    for (auto&& name : CollectGlobalBindingNames(moduleBuilder)) {
        auto binding = CompileGlobalBinding(
            moduleBuilder.Commands,
            moduleBuilder.Vars,
            moduleBuilder.GetConf(),
            name
        );
        result.push_back({
            .Name = std::move(name),
            .Binding = std::move(binding),
        });
    }
    return result;
}
