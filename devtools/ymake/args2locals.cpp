#include "args2locals.h"

#include "vars.h"

#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/lang/call_args_parser.h>
#include <devtools/ymake/lang/call_signature.h>
#include <devtools/ymake/options/static_options.h>

#include <util/generic/array_ref.h>

#include <fmt/format.h>

namespace {

std::string FormatWrongNumberOfArgumentsErr(size_t numberOfFormals, size_t numberOfActuals) {
    Y_ASSERT(numberOfFormals != numberOfActuals);
    return fmt::format("Wrong number of arguments: {} != {}", ToString(numberOfFormals), ToString(numberOfActuals));
}

TMapMacroDiagResult MapMacroVars(TArrayRef<const TStringBuf> args, const TVector<TStringBuf>& argNames, TVars& vars) {
    if (args.empty() && argNames.empty()) {
        return {};
    }
    const bool hasVarArg = argNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX); // FIXME: this incorrectly reports "true" for "macro M(X[]){...}" and suchlike
    const bool lastMayBeEmpty = argNames.size() - args.size() == 1 && hasVarArg;
    if (argNames.size() != args.size() && (argNames.empty() || (argNames.size() > args.size() && !lastMayBeEmpty))) {
        return std::unexpected(TMapMacroVarsErr{
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
                .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
            );
        }

        isLastArg = argNamesIndex >= argNames.size() - 1;
        TStringBuf name = argNames[isLastArg ? argNames.size() - 1 : argNamesIndex];
        const bool isArrayArg = name.EndsWith(NStaticConf::ARRAY_SUFFIX);

        if (!isLastArg && insideArray && !isArrayArg) {
            return std::unexpected(TMapMacroVarsErr{
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
            .Message = "Expected ']'"}
        );
    }

    if (!isLastArg) {
        return std::unexpected(TMapMacroVarsErr{
            .Message = FormatWrongNumberOfArgumentsErr(argNames.size(), args.size())}
        );
    }

    return {};
}

void VarAppend(TYVar& var, TStringBuf val) {
    var.push_back(TVarStr(val, false, false));
    if (IsPropertyVarName(var.back().Name)) {
        var.back().IsMacro = true;
    }
}

void VarAppend(TYVar& var, std::span<const TStringBuf> vals) {
    for (auto val: vals)
        VarAppend(var, val);
}

void VarAppend(TYVar& var, std::span<const TString> vals) {
    for (auto val: vals)
        VarAppend(var, val);
}

void SetDefaultsForMissingKw(const TSignature& sign, TVars& locals) {
    for (const auto& [key, kw]: sign.GetKeywords()) {
        if (!locals.Contains(key)) {
            if (!kw.OnKwMissing.empty())
                VarAppend(locals[key], kw.OnKwMissing);
            else
                VarAppend(locals[key], std::span<const TStringBuf>{});
        }
    }
}

}

void TMapMacroVarsErr::Report(TStringBuf macroName, TStringBuf argsStr) const {
    const auto what = TString::Join("macro '", macroName, "': ", Message, "\n\tArgs: ", argsStr);
    TRACE(S, NEvent::TMakeSyntaxError(what, Diag()->Where.back().second));
    YConfErr(Syntax) << what << Endl;
}

TMapMacroDiagResult AddMacroArgsToLocals(const TSignature& sign, TArrayRef<const TStringBuf> args, TVars& locals) {
    TMapMacroDiagResult res;

    for (const auto& arg: TParsedCallArgs{sign, args}) {
        if (!arg)
            return std::unexpected(TMapMacroVarsErr{.Message = arg.error().Message(sign, args)});
        if (arg->first == VarargIdx)
            VarAppend(locals[sign.GetVarargName()], arg->second);
        else if (auto kw = KeywordData(sign, arg->first)) {
            if (kw->Kind == TKeyword::Flag)
                VarAppend(locals[ArgDefName(sign, arg->first)], kw->OnKwPresent);
            else
                VarAppend(locals[ArgDefName(sign, arg->first)], arg->second);
        } else
            VarAppend(locals[ArgDefName(sign, arg->first)], arg->second);
    }

    SetDefaultsForMissingKw(sign, locals);
    if (sign.HasVararg())
        locals[sign.GetVarargName()];

    return res;
}

TMapMacroVarsResult AddMacroArgsToLocals(const TSignature& sign, TArrayRef<const TStringBuf> args) {
    TVars locals;
    return AddMacroArgsToLocals(sign, args, locals)
        .and_then([&]() -> TMapMacroVarsResult {return std::move(locals);});
}

TMapMacroDiagResult AddMacroArgsToLocals(const TSignature* sign, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals) {
    if (sign && sign->IsNonPositional()) {
        return AddMacroArgsToLocals(*sign, args, locals);
    }
    return MapMacroVars(args, argNames, locals);
}
