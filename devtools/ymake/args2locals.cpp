#include "args2locals.h"

#include <devtools/ymake/cmd_properties.h>
#include <devtools/ymake/args_converter.h>

#include <devtools/ymake/options/static_options.h>

#include <devtools/ymake/diag/trace.h>

#include <fmt/format.h>

namespace {

std::string FormatWrongNumberOfArgumentsErr(size_t numberOfFormals, size_t numberOfActuals) {
    Y_ASSERT(numberOfFormals != numberOfActuals);
    return fmt::format("Wrong number of arguments: {} != {}", ToString(numberOfFormals), ToString(numberOfActuals));
}

TMapMacroVarsResult MapMacroVars(TArrayRef<const TStringBuf> args, const TVector<TStringBuf>& argNames, TVars& vars) {
    if (args.empty() && argNames.empty()) {
        return {};
    }
    const bool hasVarArg = argNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX); // FIXME: this incorrectly reports "true" for "macro M(X[]){...}" and suchlike
    const bool lastMayBeEmpty = argNames.size() - args.size() == 1 && hasVarArg;
    if (argNames.size() != args.size() && (argNames.empty() || (argNames.size() > args.size() && !lastMayBeEmpty))) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
            .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
        );
    }

    bool isLastArg = false;
    bool isEmptyArray = false;
    bool insideArray = false;
    size_t argNamesIndex = 0;
    for (TStringBuf arg : args) {
        if (argNamesIndex >= argNames.size() && !hasVarArg) {
            return std::unexpected(TMapMacroVarsErr{
                .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
                .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
            );
        }

        isLastArg = argNamesIndex >= argNames.size() - 1;
        TStringBuf name = argNames[isLastArg ? argNames.size() - 1 : argNamesIndex];
        const bool isArrayArg = name.EndsWith(NStaticConf::ARRAY_SUFFIX);

        if (!isLastArg && insideArray && !isArrayArg) {
            return std::unexpected(TMapMacroVarsErr{
                .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                .Message = fmt::format("Passed array but expected element {}", name)}
            );
        }

        if (isArrayArg) {
            name.Chop(3);
        }

        if (arg == "SKIPPED") {
            //SBDIAG << "PMV: Skip: " << name << Endl;
            vars[name]; //leave empty value
            argNamesIndex++;
            continue;
        }

        if (arg.StartsWith(NStaticConf::ARRAY_START)) {
            if (insideArray) {
                return std::unexpected(TMapMacroVarsErr{
                    .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                    .Message = "'[' inside '[ ]'"}
                );
            }
            insideArray = true;
            isEmptyArray = true;
            arg.Skip(1);
            if (arg.empty()) {
                continue;
            }
        }

        if (arg.EndsWith(NStaticConf::ARRAY_END)) {
            if (!insideArray) {
                return std::unexpected(TMapMacroVarsErr{
                    .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                    .Message = "Expected '[' before ']'"}
                );
            }
            insideArray = false;
            arg.Chop(1);
            if (arg.empty()) {
                if (isEmptyArray) {
                    vars[name];
                }
                argNamesIndex++;
                continue;
            }
        }

        //if (name == "MnInfo") YDIAG(VV) << vars << "*************\n";
        vars[name].push_back(TVarStr(arg, false, false));
        if (IsPropertyVarName(vars[name].back().Name)) {
            vars[name].back().IsMacro = true;
        }
        isEmptyArray = false;
        //SBDIAG << "PMV : " << name << "=" << arg << Endl;
        if (!insideArray) {
            argNamesIndex++;
        }
    }

    //allowed for now
    if (argNamesIndex == argNames.size() - 1 && lastMayBeEmpty) {
        TStringBuf name = argNames.back();
        //SBDIAG << "Last argument is empty but it as an array argument, so " << name << " may be empty" << Endl;
        name.Chop(3); //was checked before (in lastMayBeEmpty initialization)
        vars[name];
        isLastArg = true;
    }

    if (insideArray) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
            .Message = "Expected ']'"}
        );
    }

    if (!isLastArg) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
            .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
        );
    }

    return {};
}

}

void TMapMacroVarsErr::Report(TStringBuf macroName, TStringBuf argsStr) const {
    const auto what = TString::Join("macro '", macroName, "': ", Message, "\n\tArgs: ", argsStr);
    TRACE(S, NEvent::TMakeSyntaxError(what, Diag()->Where.back().second));
    YConfErr(Syntax) << what << Endl;
}

TMapMacroVarsResult AddMacroArgsToLocals(const TCmdProperty* prop, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals, IMemoryPool& memPool) {
    if (prop && prop->IsNonPositional()) {
        ConvertArgsToPositionalArrays(*prop, args, memPool);
    }
    return MapMacroVars(args, argNames, locals);
}

TMapMacroVarsResult AddMacroArgsToLocals(const TCmdProperty& macroProps, TArrayRef<const TStringBuf> args, TVars& locals, IMemoryPool& memPool) {
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
        const TVector<TStringBuf> argNames{macroProps.ArgNames().begin(), macroProps.ArgNames().end()};
        return MapMacroVars(pArgs, argNames, locals);
    }
    return {};
}
