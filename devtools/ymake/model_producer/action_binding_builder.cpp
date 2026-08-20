#include "action_binding_builder.h"

#include "../add_iter.h"
#include "../command_store.h"
#include "../macro_processor.h"
#include "../macro_string.h"
#include "../module_builder.h"
#include "../ymake.h"

#include <devtools/ymake/diag/diag.h>

#include <util/stream/str.h>

TCompiledBindingExpression CompileConfigurationBinding(
    TCommandInfo& commandInfo,
    const TVector<TStringBuf>& variableNames
) {
    TStringStream cfgVars;
    for (const auto variableName : variableNames) {
        cfgVars << " " << variableName << "=$" << variableName;
    }

    YDIAG(VV) << "CFG_VARS [" << commandInfo.Module->Vars.Id << "] -> " << cfgVars.Str() << Endl;
    auto compiled = commandInfo.UpdIter->YMake.Commands.Compile(
        cfgVars.Str(),
        *commandInfo.Conf,
        commandInfo.Module->Vars,
        false,
        {}
    );
    return {.Expression = std::move(compiled.Expression)};
}

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
    TModuleBuilder& moduleBuilder,
    TStringBuf variableName
) {
    const TYVar* variable = moduleBuilder.Vars.Lookup(variableName);
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

    auto compiled = moduleBuilder.Commands.Compile(
        commandValue,
        moduleBuilder.GetConf(),
        moduleBuilder.Vars,
        false,
        {}
    );
    return TCompiledGlobalBinding{
        .CommandId = commandId,
        .CommandName = TString{commandName},
        .Value = {.Expression = std::move(compiled.Expression)},
    };
}
