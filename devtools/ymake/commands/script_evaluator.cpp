#include "script_evaluator.h"
#include "mod_registry.h"
#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/polexpr/evaluate.h>
#include <fmt/format.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {
    struct TCounterBump {
        explicit TCounterBump(size_t& counter): Counter(counter) { ++Counter; }
        ~TCounterBump() { --Counter; }
    private:
        size_t& Counter;
    };
}

struct NCommands::TArgAccumulator: TCommandSequenceWriterStubs {
public:
    TArgAccumulator(ICommandSequenceWriter* parent): Parent(parent) {
    }
public:
    // TODO we should probably accumulate then merge instead of writing directly;
    // TODO consider WriteCwd() etc.
    void WriteEnv(TStringBuf env) override {
        if (Parent)
            Parent->WriteEnv(env);
        else
            TCommandSequenceWriterStubs::WriteEnv(env);
    }
public:
    void BeginScript() override {
        Y_ABORT();
    }
    void BeginCommand() override {
        Y_ABORT();
    }
    void WriteArgument(TStringBuf arg) override {
        // TODO optimize the `TString arg` case?
        Args.emplace_back(arg);
    }
    void EndCommand() override {
        Y_ABORT();
    }
    void EndScript(TCommandInfo&, const TVars&) override {
        Y_ABORT();
    }
public:
    void AppendTerm(TTermValue&& term) {
        Y_ASSERT(!Args.empty());
        auto& arg = Args.back();
        std::visit(TOverloaded{
            [&](TTermError) {
                Y_ABORT();
            },
            [&](TTermNothing) {
            },
            [&](TString&& s) {
                // TODO optimize the `arg.empty()` case?
                arg += s;
            },
            [&](TVector<TString>&& v) {
                size_t n = 0;
                for (auto& s : v)
                    n += s.size();
                arg.reserve(arg.size() + n);
                for (auto& s : v)
                    arg += s;
            },
            [&](TTaggedStrings&&) {
                throw TNotImplemented();
            }
        }, std::move(term));
    }
    TVector<TString> Args;
private:
    ICommandSequenceWriter* const Parent;
};

inline
auto TScriptEvaluator::GetFnArgs(const NPolexpr::TExpression& expr, size_t pos, EMacroFunction expected) {
    return NPolexpr::GetFnArgs(expr, pos, Commands->Mods.Func2Id(expected));
}

TScriptEvaluator::TResult TScriptEvaluator::DoScript(
    const NPolexpr::TExpression* expr,
    size_t scrBegin,
    TErrorShowerState* errorShower,
    ICommandSequenceWriter* writer
) {
    ErrorShower = errorShower;
    auto [cmdBegin, cmdCnt] = GetFnArgs(*expr, scrBegin, EMacroFunction::Cmds);
    auto error = false;
    for (size_t cmd = 0; cmd != cmdCnt; ++cmd) {
        auto [argBegin, argCnt] = GetFnArgs(*expr, cmdBegin, EMacroFunction::Args);
        if (argCnt == 1) {
            auto [termBegin, termCnt] = GetFnArgs(*expr, argBegin, EMacroFunction::Terms);
            if (termCnt == 1) {
                if (auto subResult = DoTermAsScript(expr, termBegin, writer); subResult.End != termBegin) {
                    cmdBegin = subResult.End;
                    error |= subResult.Error;
                    continue;
                }
            }
        }
        writer->BeginCommand();
        auto subResult = DoCommand(expr, cmdBegin, writer);
        cmdBegin = subResult.End;
        error |= subResult.Error;
        writer->EndCommand();
    }
    Y_ABORT_UNLESS(cmdBegin == expr->GetNodes().size());
    Y_UNUSED(error); // TODO report it?
    return {cmdBegin};
}

TScriptEvaluator::TSubResult TScriptEvaluator::DoTermAsScript(const NPolexpr::TExpression* /*expr*/, size_t begin, ICommandSequenceWriter* /*writer*/) {
    return {begin, false}; // NYI
}

TScriptEvaluator::TSubResult TScriptEvaluator::DoCommand(const NPolexpr::TExpression* expr, size_t cmdBegin, ICommandSequenceWriter* writer) {
    auto [argBegin, argCnt] = GetFnArgs(*expr, cmdBegin, EMacroFunction::Args);
    auto error = false;
    for (size_t arg = 0; arg != argCnt; ++arg) {
        auto [termBegin, termCnt] = GetFnArgs(*expr, argBegin, EMacroFunction::Terms);
        if (termCnt == 1) {
            if (auto subResult = DoTermAsCommand(expr, termBegin, writer); subResult.End != termBegin) {
                argBegin = subResult.End;
                error |= subResult.Error;
                continue;
            }
        }
        auto subResult = DoArgument(expr, argBegin, writer);
        argBegin = subResult.End;
        error |= subResult.Error;
    }
    return {argBegin, error};
}

TScriptEvaluator::TSubResult TScriptEvaluator::DoTermAsCommand(const NPolexpr::TExpression* expr, size_t begin, ICommandSequenceWriter* writer) {
    TEvalCtx ctx{BuildConf, *Vars, *CmdInfo, *Inputs};
    auto [term, end] = ::NPolexpr::Evaluate<TTermValue>(*expr, begin, TOverloaded{
        [&](NPolexpr::TConstId id) -> TTermValue {
            auto val = Commands->Values.GetValue(id);
            return Commands->EvalConst(val, ctx);
        },
        [&](NPolexpr::EVarId id) -> TTermValue {
            auto varName = Commands->Values.GetVarName(id);
            auto var = Vars->Lookup(varName);
            if (!var)
                return TVector<TString>();
            TArgAccumulator subWriter(writer);
            bool error = false;
            bool hasPeerDirTags = false;
            bool hasNoPeerDirTags = false;
            for (auto& varStr : *var) {
                auto val = varStr.HasPrefix ? GetCmdValue(varStr.Name) : varStr.Name;
                if (!varStr.StructCmdForVars) {
                    auto finalVal = CmdInfo->SubstMacroDeeply(nullptr, val, *Vars, false);
                    auto args = SplitArgs(finalVal);
                    for (auto& arg : args)
                        subWriter.WriteArgument(arg);
                    if (varStr.HasPeerDirTags)
                        hasPeerDirTags = true;
                    else
                        hasNoPeerDirTags = true;
                    continue;
                }
                auto subExpr = AsSubexpression(val);
                if (!subExpr || subExpr->GetNodes().empty())
                    continue; // TODO?
                auto [cmdBegin, cmdCnt] = GetFnArgs(*subExpr, 0, EMacroFunction::Cmds);
                if (cmdCnt == 0)
                    continue; // empty values are currently stored as `Cmds()`
                Y_ABORT_UNLESS (cmdCnt == 1);
                auto bump = TCounterBump(ErrorDepth);
                auto subResult = DoCommand(subExpr, cmdBegin, &subWriter);
                cmdBegin = subResult.End;
                error |= subResult.Error;
                Y_DEBUG_ABORT_UNLESS(cmdBegin == subExpr->GetNodes().size());
            }
            if (hasPeerDirTags) {
                if (hasNoPeerDirTags)
                  throw yexception() << "inconsistent peerdir tags detected";
                auto& src = subWriter.Args;
                TTaggedStrings dst(src.size());
                std::transform(src.begin(), src.end(), dst.begin(), [](auto& s) {
                    auto delim = s.find("$");
                    if (delim == TString::npos)
                        return TTaggedString{.Data = s};
                    TVector<TString> tags = StringSplitter(TStringBuf(s.data(), delim)).Split(',').SkipEmpty();
                    return TTaggedString{.Data = s.substr(delim), .Tags = tags};
                });
                return dst;
            }
            return error ? TTermError(fmt::format("while substituting as {} into", varName), false) : TTermValue(subWriter.Args);
        },
        [&](NPolexpr::TFuncId id, std::span<const TTermValue> args) -> TTermValue {
            return EvalFn(id, args, ctx, writer);
        },
    });

    bool error = false;
    std::visit(TOverloaded{
        [&](const TTermError& e) {
            if (ErrorShower->Accept(ErrorDepth)) {
                (e.Origin ? YErr() : YInfo())
                    << (e.Origin ? "Expression command term evaluation error: " : "")
                    << e.Msg << "\n"
                    << Commands->PrintCmd(*expr, begin, end) << "\n" << Endl;
                error = true;
            }
        },
        [&](TTermNothing) {
        },
        [&](TString& s) {
            writer->WriteArgument(s);
        },
        [&](TVector<TString>& v) {
            for (auto& s : v)
                writer->WriteArgument(s);
        },
        [&](TTaggedStrings& v) {
            for (auto& s : v)
                writer->WriteArgument(s.Data);
        }
    }, term);

    return {end, error};
}

TScriptEvaluator::TSubResult TScriptEvaluator::DoArgument(const NPolexpr::TExpression* expr, size_t argBegin, ICommandSequenceWriter* writer) {
    TArgAccumulator args(writer);
    args.WriteArgument({});
    auto [termBegin, termCnt] = GetFnArgs(*expr, argBegin, EMacroFunction::Terms);
    bool error = false;
    for (size_t term = 0; term != termCnt; ++term) {
        auto subResult = DoTerm(expr, termBegin, &args);
        termBegin = subResult.End;
        error |= subResult.Error;
    }
    Y_ASSERT(args.Args.size() == 1);
    auto& arg = args.Args.front();
    writer->WriteArgument(arg);
    return {termBegin, error};
}

TScriptEvaluator::TSubResult TScriptEvaluator::DoTerm(
    const NPolexpr::TExpression* expr,
    size_t begin,
    TArgAccumulator* writer
) {
    TEvalCtx ctx{BuildConf, *Vars, *CmdInfo, *Inputs};
    auto [term, end] = ::NPolexpr::Evaluate<TTermValue>(*expr, begin, [&](auto id, auto&&... args) -> TTermValue {
        if constexpr (std::is_same_v<decltype(id), NPolexpr::TConstId>) {
            static_assert(sizeof...(args) == 0);
            auto val = Commands->Values.GetValue(id);
            return Commands->EvalConst(val, ctx);
        }
        else if constexpr (std::is_same_v<decltype(id), NPolexpr::EVarId>) {
            static_assert(sizeof...(args) == 0);
            auto varName = Commands->Values.GetVarName(id);
            auto var = Vars->Lookup(varName);
            if (!var)
                return TVector<TString>();
            TArgAccumulator subWriter(writer);
            auto error = false;
            for (auto& varStr : *var) {
                auto val = varStr.HasPrefix ? GetCmdValue(varStr.Name) : varStr.Name;
                if (!varStr.StructCmdForVars) {
                    // apparently, we get post-substitution values here,
                    // so all that's left is to split the results
                    for (auto&& s : SplitArgs(TString(val))) // TODO avoid making a TString
                        subWriter.WriteArgument(std::move(s));
                    continue;
                }
                auto subExpr = AsSubexpression(val);
                if (!subExpr)
                    continue; // TODO?
                auto [cmdView, cmdCnt] = GetFnArgs(*subExpr, 0, EMacroFunction::Cmds);
                if (cmdCnt == 1) {
                    auto [argView, argCnt] = GetFnArgs(*subExpr, cmdView, EMacroFunction::Args);
                    for (size_t arg = 0; arg != argCnt; ++arg) {
                        subWriter.WriteArgument({});
                        auto [termView, termCnt] = GetFnArgs(*subExpr, argView, EMacroFunction::Terms);
                        for (size_t term = 0; term != termCnt; ++term) {
                            auto bump = TCounterBump(ErrorDepth);
                            auto subResult = DoTerm(subExpr, termView, &subWriter);
                            termView = subResult.End;
                            error |= subResult.Error;
                        }
                        argView = termView;
                    }
                    cmdView = argView;
                }
                Y_DEBUG_ABORT_UNLESS(cmdView == subExpr->GetNodes().size());
            }
            return error ? TTermError(fmt::format("while substituting as {} into", varName), false) : TTermValue(subWriter.Args);
        }
        else if constexpr (std::is_same_v<decltype(id), NPolexpr::TFuncId>) {
            static_assert(sizeof...(args) == 1);
            return EvalFn(id, std::forward<decltype(args)...>(args...), ctx, writer);
        }
    });

    if (auto e = std::get_if<TTermError>(&term)) {
        if (ErrorShower->Accept(ErrorDepth)) {
            (e->Origin ? YErr() : YInfo())
                << (e->Origin ? "Expression term evaluation error: " : "")
                << e->Msg << "\n"
                << Commands->PrintCmd(*expr, begin, end) << "\n" << Endl;
            return {end, true};
        }
    } else {
        writer->AppendTerm(std::move(term));
    }

    return {end, false};
}

const NPolexpr::TExpression* TScriptEvaluator::AsSubexpression(const TStringBuf& val) {
    return Commands->Get(val, CmdConf);
}

const NPolexpr::TExpression* TScriptEvaluator::AsSubexpression(const TYVar* var) {
    if (CmdConf && var && var->size() >= 1 && var->front().StructCmdForVars) {
        auto val = Eval1(var);
        return AsSubexpression(val);
    }
    return nullptr;
}

TTermValue TScriptEvaluator::EvalFn(
    NPolexpr::TFuncId id, std::span<const TTermValue> args,
    const TEvalCtx& ctx,
    ICommandSequenceWriter* writer
) {
    for (auto& arg : args)
        if (std::holds_alternative<TTermError>(arg))
            return arg;
    auto fn = Commands->Mods.Id2Func(id);
    try {
        auto mod = Commands->Mods.At(fn);
        Y_DEBUG_ABORT_UNLESS(mod);
        try {
            if (Y_LIKELY(mod))
                return mod->Evaluate(args, ctx, writer);
        } catch (TNotSupported) {
            // fall through
        }
        throw yexception()
            << "Don't know how to render configure time modifier "
            << fn;
    } catch (const std::exception& e) {
        ++ErrorShower->Count;
        return TTermError(e.what(), true);
    }
}
