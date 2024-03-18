#include "script_evaluator.h"
#include "function_evaluator.h"
#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/polexpr/evaluate.h>
#include <fmt/format.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

struct NCommands::TArgAccumulator: TCommandSequenceWriterStubs {
    virtual void BeginScript() {
        Y_ABORT();
    }
    virtual void BeginCommand() {
        Y_ABORT();
    }
    virtual void WriteArgument(TStringBuf arg) {
        Args.push_back(TString(arg));
    }
    virtual void EndCommand() {
        Y_ABORT();
    }
    virtual void EndScript(TCommandInfo&, const TVars&) {
        Y_ABORT();
    }
public:
    TVector<TString> Args;
};

inline
auto TScriptEvaluator::GetFnArgs(const NPolexpr::TExpression& expr, size_t pos, EMacroFunctions expected) {
    return NPolexpr::GetFnArgs(expr, pos, Commands->Values.Func2Id(expected));
}

size_t TScriptEvaluator::DoScript(const NPolexpr::TExpression* expr, size_t scrBegin, ICommandSequenceWriter* writer) {
    auto [cmdBegin, cmdCnt] = GetFnArgs(*expr, scrBegin, EMacroFunctions::Cmds);
    for (size_t cmd = 0; cmd != cmdCnt; ++cmd) {
        auto [argBegin, argCnt] = GetFnArgs(*expr, cmdBegin, EMacroFunctions::Args);
        if (argCnt == 1) {
            auto [termBegin, termCnt] = GetFnArgs(*expr, argBegin, EMacroFunctions::Terms);
            if (termCnt == 1) {
                if (auto termEnd = DoTermAsScript(expr, termBegin, writer); termEnd != termBegin) {
                    cmdBegin = termEnd;
                    continue;
                }
            }
        }
        writer->BeginCommand();
        cmdBegin = DoCommand(expr, cmdBegin, writer);
        writer->EndCommand();
    }
    Y_ABORT_UNLESS(cmdBegin == expr->GetNodes().size());
    return cmdBegin;
}

size_t TScriptEvaluator::DoTermAsScript(const NPolexpr::TExpression* /*expr*/, size_t begin, ICommandSequenceWriter* /*writer*/) {
    return begin; // NYI
}

size_t TScriptEvaluator::DoCommand(const NPolexpr::TExpression* expr, size_t cmdBegin, ICommandSequenceWriter* writer) {
    auto [argBegin, argCnt] = GetFnArgs(*expr, cmdBegin, EMacroFunctions::Args);
    for (size_t arg = 0; arg != argCnt; ++arg) {
        auto [termBegin, termCnt] = GetFnArgs(*expr, argBegin, EMacroFunctions::Terms);
        if (termCnt == 1) {
            if (auto termEnd = DoTermAsCommand(expr, termBegin, writer); termEnd != termBegin) {
                argBegin = termEnd;
                continue;
            }
        }
        argBegin = DoArgument(expr, argBegin, writer);
    }
    return argBegin;
}

size_t TScriptEvaluator::DoTermAsCommand(const NPolexpr::TExpression* expr, size_t termBegin, ICommandSequenceWriter* writer) {
    TEvalCtx ctx{*Vars, *CmdInfo, *Inputs};
    auto [term, end] = ::NPolexpr::Evaluate<TTermValue>(*expr, termBegin, TOverloaded{
        [&](NPolexpr::TConstId id) -> TTermValue {
            auto val = Commands->Values.GetValue(id);
            if (auto inputs = std::get_if<TMacroValues::TInputs>(&val); inputs) {
                auto result = TVector<TString>();
                for (auto& coord : inputs->Coords) {
                    auto inputResult = Commands->InputToStringArray(TMacroValues::TInput {.Coord = coord}, ctx);
                    result.insert(result.end(), inputResult.begin(), inputResult.end());
                }
                return result;
            }
            if (auto input = std::get_if<TMacroValues::TInput>(&val); input) {
                return Commands->InputToStringArray(*input, ctx);
            }
            return TString(Commands->ConstToString(val, ctx));
        },
        [&](NPolexpr::EVarId id) -> TTermValue {
            auto var = Vars->Lookup(Commands->Values.GetVarName(id));
            if (!var)
                return TVector<TString>();
            TArgAccumulator subWriter;
            for (auto& varStr : *var) {
                auto val = varStr.HasPrefix ? GetCmdValue(varStr.Name) : varStr.Name;
                if (!varStr.StructCmd) {
                    auto finalVal = CmdInfo->SubstMacroDeeply(nullptr, val, *Vars, false);
                    auto args = SplitArgs(finalVal);
                    for (auto& arg : args)
                        subWriter.WriteArgument(arg);
                    continue;
                }
                auto subExpr = AsSubexpression(val);
                if (!subExpr)
                    continue; // TODO?
                auto [cmdBegin, cmdCnt] = GetFnArgs(*subExpr, 0, EMacroFunctions::Cmds);
                if (cmdCnt == 0)
                    continue; // empty values are currently stored as `Cmds()`
                Y_ABORT_UNLESS (cmdCnt == 1);
                cmdBegin = DoCommand(subExpr, cmdBegin, &subWriter);
                Y_DEBUG_ABORT_UNLESS(cmdBegin == subExpr->GetNodes().size());
            }
            return subWriter.Args;
        },
        [&](NPolexpr::TFuncId id, std::span<const TTermValue> args) -> TTermValue {
            return EvalFn(id, args, ctx, expr, writer);
        },
    });

    std::visit(TOverloaded{
        [&](std::monostate) {
        },
        [&](TString& s) {
            writer->WriteArgument(s);
        },
        [&](TVector<TString>& v) {
            for (auto& s : v)
                writer->WriteArgument(s);
        }
    }, term);

    return end;
}

size_t TScriptEvaluator::DoArgument(const NPolexpr::TExpression* expr, size_t argBegin, ICommandSequenceWriter* writer) {
    TArgAccumulator args;
    auto [termBegin, termCnt] = GetFnArgs(*expr, argBegin, EMacroFunctions::Terms);
    for (size_t term = 0; term != termCnt; ++term) {
        termBegin = DoTerm(expr, termBegin, &args);
    }
    for (auto&& arg : args.Args)
        if (!arg.empty())
            writer->WriteArgument(arg);
    return termBegin;
}

size_t TScriptEvaluator::DoTerm(
    const NPolexpr::TExpression* expr,
    size_t begin,
    TArgAccumulator* writer
) {
    TEvalCtx ctx{*Vars, *CmdInfo, *Inputs};
    auto [term, end] = ::NPolexpr::Evaluate<TTermValue>(*expr, begin, [&](auto id, auto&&... args) -> TTermValue {
        if constexpr (std::is_same_v<decltype(id), NPolexpr::TConstId>) {
            static_assert(sizeof...(args) == 0);
            auto val = Commands->Values.GetValue(id);
            if (auto inputs = std::get_if<TMacroValues::TInputs>(&val); inputs) {
                ythrow TNotImplemented();
            }
            if (auto input = std::get_if<TMacroValues::TInput>(&val); input) {
                return Commands->InputToStringArray(*input, ctx);
            }
            return TString(Commands->ConstToString(val, ctx));
        }
        else if constexpr (std::is_same_v<decltype(id), NPolexpr::EVarId>) {
            static_assert(sizeof...(args) == 0);
            auto var = Vars->Lookup(Commands->Values.GetVarName(id));
            auto subExpr = AsSubexpression(var);
            if (!subExpr)
                return ::EvalAllSplit(var);
            TArgAccumulator subWriter;
            auto [cmdView, cmdCnt] = GetFnArgs(*subExpr, 0, EMacroFunctions::Cmds);
            if (cmdCnt == 1) {
                auto [argView, argCnt] = GetFnArgs(*subExpr, cmdView, EMacroFunctions::Args);
                for (size_t arg = 0; arg != argCnt; ++arg) {
                    auto [termView, termCnt] = GetFnArgs(*subExpr, argView, EMacroFunctions::Terms);
                    for (size_t term = 0; term != termCnt; ++term) {
                        termView = DoTerm(subExpr, termView, &subWriter);
                    }
                    argView = termView;
                }
                cmdView = argView;
            }
            Y_DEBUG_ABORT_UNLESS(cmdView == subExpr->GetNodes().size());

            return subWriter.Args;
        }
        else if constexpr (std::is_same_v<decltype(id), NPolexpr::TFuncId>) {
            static_assert(sizeof...(args) == 1);
            return EvalFn(id, std::forward<decltype(args)...>(args...), ctx, expr, writer);
        }
    });

    AppendTerm(std::move(term), writer);

    return end;
}

const NPolexpr::TExpression* TScriptEvaluator::AsSubexpression(const TStringBuf& val) {
    return Commands->Get(val, CmdConf);
}

const NPolexpr::TExpression* TScriptEvaluator::AsSubexpression(const TYVar* var) {
    if (CmdConf && var && var->size() >= 1 && var->front().StructCmd) {
        auto val = Eval1(var);
        return AsSubexpression(val);
    }
    return nullptr;
}

TTermValue TScriptEvaluator::EvalFn(
    NPolexpr::TFuncId id, std::span<const TTermValue> args,
    const TEvalCtx& ctx, const NPolexpr::TExpression* expr,
    ICommandSequenceWriter* writer
) {
    switch (Commands->Values.Id2Func(id)) {
        case EMacroFunctions::Args: return RenderArgs(args);
        case EMacroFunctions::Terms: return RenderTerms(args);
        case EMacroFunctions::Hide: return std::monostate();
        case EMacroFunctions::Clear: return RenderClear(args);
        case EMacroFunctions::Pre: return RenderPre(args);
        case EMacroFunctions::Suf: return RenderSuf(args);
        case EMacroFunctions::Quo: return RenderQuo(args);
        case EMacroFunctions::SetEnv: RenderEnv(writer, ctx, args); return {};
        case EMacroFunctions::CutExt: return RenderCutExt(args);
        case EMacroFunctions::LastExt: return RenderLastExt(args);
        case EMacroFunctions::ExtFilter: return RenderExtFilter(args);
        case EMacroFunctions::KeyValue: RenderKeyValue(ctx, args); return {};
        case EMacroFunctions::TODO1: return RenderTODO1(args);
        case EMacroFunctions::TODO2: return RenderTODO2(args);
        case EMacroFunctions::MsvsSource: return RenderMsvsSource(writer, args);
        default:
            break;
    }
    throw yexception()
        << "Don't know how to render configure time modifier "
        << Commands->Values.Id2Func(id)
        << " in expression: " + Commands->PrintCmd(*expr);
}

void TScriptEvaluator::AppendTerm(TTermValue&& term, TArgAccumulator* writer) {
    if (writer->Args.empty()) {
        std::visit(TOverloaded{
            [&](std::monostate) {
            },
            [&](TString&& s) {
                writer->Args.push_back(std::move(s));
            },
            [&](TVector<TString>&& v) {
                writer->Args = std::move(v);
            }
        }, std::move(term));
    } else {
        std::visit(TOverloaded{
            [&](std::monostate) {
            },
            [&](TString&& s) {
                for (auto& arg : writer->Args)
                    arg += s;
            },
            [&](TVector<TString>&& v) {
                auto result = fmt::format("{}{}", fmt::join(writer->Args, " "), fmt::join(v, " "));
                writer->Args = {TString(std::move(result))};
            }
        }, std::move(term));
    }
}
