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

TStringBuf GetVararg(const TCmdProperty& props) noexcept {
    if (props.GetNumUsrArgs() == 0 || !props.ArgNames().back().EndsWith(NStaticConf::ARRAY_SUFFIX))
        return {};

    TStringBuf res{props.ArgNames().back()};
    res.remove_suffix(3);
    return res;
}

template<std::convertible_to<TStringBuf> TStr>
TStringBuf TakeArg(std::span<const TStr>& args) noexcept {
    if (args.empty())
        return  {};
    TStringBuf res = args.front();
    args = args.subspan(1);
    return res;
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

class TKWArgs {
public:
    static std::pair<std::span<const TStringBuf>, TKWArgs> Find(const TCmdProperty &props, std::span<const TStringBuf> args) {
        TKWArgs tail{props, args};
        const auto nonKw = tail.TakeUntilKeyword();
        return {nonKw, tail};
    }

    struct TKeywordVals {
        const TKeyword& Kw;
        std::span<const TStringBuf> Args;
    };
    std::optional<TKeywordVals> TakeNext() {
        if (!NextKW_)
            return std::nullopt;
        auto* lastKw = NextKW_;
        return TKeywordVals{*lastKw, TakeUntilKeyword()};
    }

    TStringBuf KeywordName(const TKeyword& kw) const {
        return Props_->GetKeyword(kw.Pos);
    }

private:
    TKWArgs(const TCmdProperty& props, std::span<const TStringBuf> args)
        : RemainingArgs_{args}
        , Props_{&props}
    {}

    std::span<const TStringBuf> TakeUntilKeyword() {
        for (size_t i = 0; i < RemainingArgs_.size(); ++i) {
            if (NextKW_ = Props_->GetKeywordData(RemainingArgs_[i])) {
                const auto res = RemainingArgs_.subspan(0, i);
                RemainingArgs_ = RemainingArgs_.subspan(i + 1);
                return res;
            }
        }
        NextKW_ = nullptr;
        return std::exchange(RemainingArgs_, {});
    }

private:
    std::span<const TStringBuf> RemainingArgs_;
    const TKeyword* NextKW_ = nullptr;
    const TCmdProperty* Props_ = nullptr;
};

TMapMacroVarsResult ConsumePositionals(
    std::span<const TStringBuf> args,
    std::span<const TString>& unsetScalars,
    TStringBuf vararg,
    TVars& locals
) {
    while (!unsetScalars.empty()) {
        if (args.empty())
            return {};

        const auto scalar = TakeArg(unsetScalars);
        const auto arg = TakeArg(args);
        VarAppend(locals[scalar], arg);
    }

    if (args.empty())
        return {};
    if (vararg.empty()) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
            .Message = fmt::format("More positional arguments passed then expected. Can't handle args '{}'", fmt::join(args, ", "))
        });
    }
    VarAppend(locals[vararg], args);
    return {};
}

void SetDefaultsForMissingKw(const TCmdProperty& props, TVars& locals) {
    for (const auto& [key, kw]: props.GetKeywords()) {
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

TMapMacroVarsResult AddMacroArgsToLocals(const TCmdProperty& props, TArrayRef<const TStringBuf> args, TVars& locals) {
    TMapMacroVarsResult res;

    const TStringBuf vararg = GetVararg(props);
    auto positionalScalars = std::span{props.ArgNames()}.subspan(props.GetKeyArgsNum());
    if (!vararg.empty())
        positionalScalars = positionalScalars.subspan(0, positionalScalars.size() - 1);

    auto [frontPosArgs, kwArgs] = TKWArgs::Find(props, args);
    res = ConsumePositionals(frontPosArgs, positionalScalars, vararg, locals);
    if (!res)
        return res;
    TStringBuf lastArr = {};
    while (auto kwVal = kwArgs.TakeNext()) {
        if (kwVal->Args.size() < kwVal->Kw.From) {
            return std::unexpected(TMapMacroVarsErr{
                .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
                .Message = fmt::format(
                    "Keyword {} requires from {} to {} arguments but got '{}'",
                    kwArgs.KeywordName(kwVal->Kw),
                    kwVal->Kw.From,
                    kwVal->Kw.To,
                    fmt::join(kwVal->Args, ", ")
                )
            });
        }

        const bool isArr = kwVal->Kw.To > 1;

        // TODO(svidyuk) better deduplication please!!!
        if (!isArr && locals.contains(kwArgs.KeywordName(kwVal->Kw))) {
            return std::unexpected(TMapMacroVarsErr{
                .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                .Message = fmt::format(
                    "Keyword {} appears more than once and is not an array",
                    kwArgs.KeywordName(kwVal->Kw)
                )
            });
        }
        if (isArr)
            lastArr = kwArgs.KeywordName(kwVal->Kw);

        if (kwVal->Args.size() > kwVal->Kw.To) {
            const auto posArgs = kwVal->Args.subspan(kwVal->Kw.To);
            if (!lastArr.empty()) {
                return std::unexpected(TMapMacroVarsErr{
                    .ErrorClass = EMapMacroVarsErrClass::ArgsSequenceError,
                    .Message = fmt::format(
                        "Mixing positional arguments with named arrays is error prone and forbidden.\n"
                        "Got positional args '{}' after named array {} have been started",
                        fmt::join(posArgs, ", "),
                        lastArr
                    )
                });
            }
            res = ConsumePositionals(posArgs, positionalScalars, vararg, locals);
            if (!res)
                return res;
            kwVal->Args = kwVal->Args.subspan(0, kwVal->Kw.To);
        }

        if (kwVal->Args.empty() && !kwVal->Kw.OnKwPresent.empty()) {
            VarAppend(locals[kwArgs.KeywordName(kwVal->Kw)], kwVal->Kw.OnKwPresent);
        } else
            VarAppend(locals[kwArgs.KeywordName(kwVal->Kw)], kwVal->Args);
    }

    if (!positionalScalars.empty()) {
        return std::unexpected(TMapMacroVarsErr{
            .ErrorClass = EMapMacroVarsErrClass::UserSyntaxError,
            .Message = fmt::format("Value for '{}' argument is missing", positionalScalars.front())
        });
    }

    SetDefaultsForMissingKw(props, locals);
    if (!vararg.empty() && !locals.contains(vararg))
        locals[vararg] = {};

    return res;
}

TMapMacroVarsResult AddMacroArgsToLocals(const TCmdProperty* prop, const TVector<TStringBuf>& argNames, TVector<TStringBuf>& args, TVars& locals) {
    if (prop && prop->IsNonPositional()) {
        return AddMacroArgsToLocals(*prop, args, locals);
    }
    return MapMacroVars(args, argNames, locals);
}
