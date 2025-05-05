#include "args2locals.h"

#include <devtools/ymake/cmd_properties.h>
#include <devtools/ymake/args_converter.h>

std::expected<void, TMapMacroVarsErr> AddMacroArgsToLocals(const TCmdProperty* prop, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals, IMemoryPool& memPool) {
    if (prop && prop->IsNonPositional()) {
        ConvertArgsToPositionalArrays(*prop, args, memPool);
    }
    return MapMacroVars(TVector<TMacro>{args.begin(), args.end()}, argNames, locals);
}

void AddMacroArgsToLocals(const TCmdProperty& macroProps, TArrayRef<const TStringBuf> args, TVars& locals, IMemoryPool& memPool) {
    // Patch incoming args to handle named macro arguments
    auto pArgs = args;
    TVector<TStringBuf> tempArgs;
    if (macroProps.IsNonPositional()) {
        tempArgs.insert(tempArgs.end(), args.begin(), args.end());
        ConvertArgsToPositionalArrays(macroProps, tempArgs, memPool);
        pArgs = tempArgs;
    }

    // Map arguments to local call vars using macro signature
    if (macroProps.ArgNames().size()) {
        const TVector<TMacro> mcrargs{pArgs.begin(), pArgs.end()};
        const TVector<TStringBuf> argNames{macroProps.ArgNames().begin(), macroProps.ArgNames().end()};
        MapMacroVars(mcrargs, argNames, locals).or_else([&](const TMapMacroVarsErr& err) -> std::expected<void, TMapMacroVarsErr> {
            err.Report(JoinStrings(args.begin(), args.end(), ", "));
            return {};
        }).value();
    }
}
