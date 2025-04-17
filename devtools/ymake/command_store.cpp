#include "command_store.h"

#include <devtools/ymake/commands/preeval.h>
#include <devtools/ymake/commands/preeval_reducer.h>

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/commands/compilation.h>
#include <devtools/ymake/commands/script_evaluator.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/lang/cmd_parser.h>
#include <devtools/ymake/polexpr/evaluate.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/common/string.h>
#include <devtools/ymake/conf.h>

#include <fmt/format.h>
#include <fmt/args.h>
#include <util/generic/overloaded.h>
#include <util/generic/scope.h>
#include <ranges>

struct TCommands::TInliner::TScope {
    explicit TScope(const TScope *base): Base(base) {}
public:
    const TScope* Base;
    using TVarDefinitions = THashMap<TStringBuf, NCommands::TSyntax>;
    TVarDefinitions VarDefinitions;
};

namespace {

    static const auto True = TString("yes");
    static const auto False = TString("no");

    inline ui64 GoodHash(const NPolexpr::TExpression& expr) noexcept {
        auto bytes = std::as_bytes(expr.GetNodes());
        return CityHash64(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

}

const NPolexpr::TExpression* TCommands::Get(TStringBuf name, const TCmdConf *conf) const {
    Y_DEBUG_ABORT_UNLESS(conf);
    Y_ABORT_UNLESS(name.StartsWith("S:"));
    const auto cmdId =
        static_cast<ECmdId>(
            FromString<std::underlying_type_t<ECmdId>>(
                name.substr(2)));
    const auto* subExpr = Get(cmdId);
    Y_DEBUG_ABORT_UNLESS(subExpr == GetByElemId(conf->GetId(name)));
    return subExpr;
}

const NCommands::TSyntax& TCommands::Parse(const TBuildConfiguration& conf, const NCommands::TModRegistry& mods, TMacroValues& values, TString src) {
    auto result = ParserCache.find(src);
    if (result == ParserCache.end())
        result = ParserCache.emplace(src, NCommands::Parse(&conf, mods, values, src)).first;
    return result->second;
}

TCommands::TInliner::TVarDefinition
TCommands::TInliner::GetVariableDefinition(NPolexpr::EVarId id) {
    auto name = Commands.Values.GetVarName(id);

    if (auto var = VarLookup(name))
        return {var, ELegacyMode::None};

    // legacy lookup

    if (
        Y_UNLIKELY(name.ends_with("__NO_UID__")) ||
        Y_UNLIKELY(name.ends_with("__NOINLINE__")) ||
        Y_UNLIKELY(name.ends_with("__LATEOUT__"))
    )
        return {};

    auto blockData = Conf.BlockData.find(name);
    if (blockData != Conf.BlockData.end())
        if (blockData->second.IsUserMacro || blockData->second.IsGenericMacro || blockData->second.IsFileGroupMacro)
            return {}; // TODO how do we _actually_ detect non-vars?

    auto var = LegacyVars.VarLookup(name, Conf);
    if (!var)
        return {};
    if (var->empty())
        // this is a weird setup,
        // but it's reachable by not assigning any values (even the empty one)
        // and instead making a block-data via `macro MYVAR() {...}` without a `.CMD`,
        // so it's not actually a macro, but an attribute/property setter of sorts
        // (a motivating example as of this writing: `CFG_VARS`)
        return {};

    // check recursion

    auto depth = LegacyVars.RecursionDepth.try_emplace(id, 0).first;
    for (size_t i = 0; i != depth->second; ++i) {
        var = var->BaseVal;
        if (!var || var->DontExpand || var->IsReservedName)
            ythrow TError() << "self-contradictory variable definition (" << name << ")";
    }

    // check the parser cache

    auto defs = LegacyVars.DefinitionCache.find(name);
    if (defs == LegacyVars.DefinitionCache.end())
        defs = LegacyVars.DefinitionCache.try_emplace(name, MakeHolder<TLegacyVars::TDefinitions>()).first;
    else
        if (depth->second < defs->second->size()) {
            auto& def = (*defs->second)[depth->second];
            return {def.Definition.Get(), def.LegacyMode}; // already processed
        }
    Y_ASSERT(defs->second->size() == depth->second);

    // do the thing

    auto def = TLegacyVars::TDefinition();
    def.LegacyMode = ELegacyMode::Expr;
    if (var->size() == 1 && var->front().HasPrefix) {
        // a conventional variable
        ui64 scopeId;
        TStringBuf cmdName;
        TStringBuf cmdValue = var->front().Name;
        NCommands::TSyntax syntax;
        ParseLegacyCommandOrSubst(var->front().Name, scopeId, cmdName, cmdValue);
        def.Definition = MakeHolder<NCommands::TSyntax>(Commands.Parse(Conf, Commands.Mods, Commands.Values, TString(cmdValue)));
    } else {
        auto has_nested_macro = false;
        auto has_no_nested_macro = false;
        for (auto& val : *var)
            (val.IsMacro ? has_nested_macro : has_no_nested_macro) = true;
        if (has_nested_macro && has_no_nested_macro)
            ythrow TError() << "inconsistent variable array items (" << name << ")";
        def.Definition = MakeHolder<NCommands::TSyntax>();
        if (has_nested_macro) {
            // a passthrough hack pretending to be "a macro" (used by _LATE_GLOB)
            auto late_globs = TMacroValues::TLegacyLateGlobPatterns();
            for (auto& val : *var)
                late_globs.Data.push_back(val.Name);
            def.Definition->Script = {{{Commands.Values.InsertValue(late_globs)}}};
            def.LegacyMode = ELegacyMode::Macro;
        }
        if (has_no_nested_macro) {
            // a proper macro argument
            auto val = GetAll(var);
            auto _val = Commands.Parse(Conf, Commands.Mods, Commands.Values, val);
            def.Definition->Script = std::move(_val.Script);
        }
    }
    defs->second->push_back(std::move(def));
    auto& result = defs->second->back();
    return {result.Definition.Get(), result.LegacyMode};
}

const NCommands::TSyntax*
TCommands::TInliner::GetMacroDefinition(NPolexpr::EVarId id) {
    auto name = Commands.Values.GetVarName(id);

    // TODO preliminary validation

    // legacy lookup

    auto var = LegacyVars.VarLookup(name, Conf);

    // check recursion

    Y_ASSERT(!LegacyVars.RecursionDepth.contains(id)); // it's a variable-only feature

    // validate contents

    if (!var) [[unlikely]]
        ythrow TError() << "unknown macro: " << name;
    if (!(var->size() == 1 && var->front().HasPrefix)) [[unlikely]]
        ythrow TError() << "not a proper macro: " << name;
    ui64 subId;
    TStringBuf subName, subValue;
    ParseLegacyCommandOrSubst(var->front().Name, subId, subName, subValue);
    Y_ASSERT(name == subName);
    //Y_ASSERT(GetMacroType(subValue) == EMT_MacroDef); // this breaks on empty .CMD, returns EMT_MacroCall

    // check the parser cache

    auto defs = LegacyVars.DefinitionCache.find(name);
    if (defs == LegacyVars.DefinitionCache.end())
        defs = LegacyVars.DefinitionCache.try_emplace(name, MakeHolder<TLegacyVars::TDefinitions>()).first;
    else {
        Y_ASSERT(defs->second->size() == 1);
        Y_ASSERT(defs->second->back().LegacyMode == ELegacyMode::Macro);
        return defs->second->back().Definition.Get(); // already processed
    }
    Y_ASSERT(defs->second->empty());

    // do the thing

    auto defBody = MacroDefBody(subValue);
    defs->second->push_back({
        MakeHolder<NCommands::TSyntax>(Commands.Parse(Conf, Commands.Mods, Commands.Values, TString(defBody))),
        ELegacyMode::Macro
    });
    return defs->second->back().Definition.Get();
}

const NCommands::TSyntax* TCommands::TInliner::VarLookup(TStringBuf name) {
    for (auto scope = Scope; scope; scope = scope->Base) {
        auto it = scope->VarDefinitions.find(name);
        if (it != scope->VarDefinitions.end())
            return &it->second;
    }
    return {};
}

const TYVar* TCommands::TInliner::TLegacyVars::VarLookup(TStringBuf name, const TBuildConfiguration& conf) {
    auto var = Vars.Lookup(name);
    if (!var || var->DontExpand || var->NoInline)
        return {};
    Y_ASSERT(!var->BaseVal || !var->IsReservedName); // TODO find out is this is so
    if (TCommandInfo::IsGlobalReservedVar(name, conf.CommandConf))
        return {};

    return var;
}

struct TCommands::TCmdWriter {

    // TODO .reserve hints
    TCmdWriter(NCommands::TSyntax& s, bool argMode): S(s), ArgMode(argMode) {}

    void BeginCommand()  { S.Script.emplace_back(); }
    void BeginArgument() { S.Script.back().emplace_back(); }
    template<typename T>
    void WriteTerm(T t)  {
        S.Script.back().back().push_back(
            ArgMode
            ? LockVariables(std::move(t))
            : UnlockVariables(std::move(t))
        );
    }
    void EndArgument()   { if (S.Script.back().back().empty()) S.Script.back().pop_back(); }
    void EndCommand()    { if (S.Script.back().empty()) S.Script.pop_back(); }

private:
    template<typename T>
    NCommands::TSyntax::TTerm LockVariables(T&& x) {
        return std::forward<T>(x);
    }
    template<>
    NCommands::TSyntax::TTerm LockVariables(NCommands::TSyntax::TTerm&& x) {
        return std::visit([&](auto&& y) {return LockVariables(std::move(y));}, std::move(x));
    }
    template<>
    NCommands::TSyntax::TTerm LockVariables(NPolexpr::EVarId&& x) {
        return NCommands::TSyntax::TUnexpanded(x);
    }
    template<>
    NCommands::TSyntax::TTerm LockVariables(NCommands::TSyntax::TTransformation&& x) {
        for (auto& arg : x.Body)
            for (auto& term : arg)
                term = LockVariables(std::move(term));
        for (auto& mod : x.Mods)
            for (auto& arg : mod.Arguments)
                for (auto& term : arg)
                    term = LockVariables(std::move(term));
        return std::move(x);
    }

private:
    template<typename T>
    NCommands::TSyntax::TTerm UnlockVariables(T&& x) {
        return std::forward<T>(x);
    }
    template<>
    NCommands::TSyntax::TTerm UnlockVariables(NCommands::TSyntax::TTerm&& x) {
        return std::visit([&](auto&& y) {return UnlockVariables(std::move(y));}, std::move(x));
    }
    template<>
    NCommands::TSyntax::TTerm UnlockVariables(NCommands::TSyntax::TUnexpanded&& x) {
        return x.Variable;
    }
    template<>
    NCommands::TSyntax::TTerm UnlockVariables(NCommands::TSyntax::TTransformation&& x) {
        for (auto& arg : x.Body)
            for (auto& term : arg)
                term = UnlockVariables(std::move(term));
        for (auto& mod : x.Mods)
            for (auto& arg : mod.Arguments)
                for (auto& term : arg)
                    term = UnlockVariables(std::move(term));
        return std::move(x);
    }

private:
    NCommands::TSyntax& S;
    const bool ArgMode;

};

//
//
//

void TCommands::TInliner::FillMacroArgs(const NCommands::TSyntax::TCall& src, TScope& dst) {
    auto argCnt = src.Arguments.size();
    for (size_t i = 0; i != argCnt; ++i) {
        auto argName = src.ArgumentNames[i];
        auto writer = TCmdWriter(dst.VarDefinitions[argName], true);
        InlineCommands(src.Arguments[i].Script, writer);
#if 0
        YDebug()
            << "macro args eval for " << Commands.Values.GetVarName(src.Function) << "[" << i << "]: "
            << Commands.PrintExpr(src.Arguments[i]) << " -> "
            << Commands.PrintExpr(dst.VarDefinitions[argName]) << Endl;
#endif
    }
}

void TCommands::TInliner::InlineModArgTerm(
    const NCommands::TSyntax::TTerm& term,
    NCommands::TSyntax::TArgument& writer
) {
    ++Depth;
    Y_DEFER {--Depth;};
    CheckDepth();
    std::visit(TOverloaded{
        [&](NPolexpr::TConstId id) {
            writer.push_back(id);
        },
        [&](NPolexpr::EVarId id) {
            auto def = GetVariableDefinition(id);
            if (!def.Definition) {
                writer.push_back(id);
                return;
            }
            if (def.Definition->Script.size() == 0)
                return;
            if (def.Definition->Script.size() != 1)
                ythrow TError() << "unexpected multicommand substitution: " << Commands.Values.GetVarName(id);
            if (def.Definition->Script[0].size() == 0)
                return;
            if (def.Definition->Script[0].size() != 1) {
                if (def.LegacyMode != ELegacyMode::None && LegacyVars.RecursionDepth[id] != 0)
                    ythrow TError() << "unexpected multiargument substitution: " << Commands.Values.GetVarName(id);
                // leave it for the actual evaluation to expand
                writer.push_back(id);
                return;
            }
            for (auto&& subTerm : def.Definition->Script[0][0]) {
                if (def.LegacyMode != ELegacyMode::None) {
                    Y_ASSERT(LegacyVars.RecursionDepth.contains(id));
                    ++LegacyVars.RecursionDepth[id];
                    Y_DEFER {--LegacyVars.RecursionDepth[id];};
                    InlineModArgTerm(subTerm, writer);
                } else
                    InlineModArgTerm(subTerm, writer);
            }
        },
        [&](const NCommands::TSyntax::TTransformation& xfm) {
            auto newXfm = NCommands::TSyntax::TTransformation();
            InlineTransformation(xfm, newXfm);
            writer.push_back(std::move(newXfm));
        },
        [&](const NCommands::TSyntax::TCall&) {
            throw TNotImplemented();
        },
        [&](const NCommands::TSyntax::TBuiltinIf&) {
            throw TNotImplemented();
        },
        [&](const NCommands::TSyntax::TIdOrString&) {
            Y_ABORT();
        },
        [&](const NCommands::TSyntax::TUnexpanded& x) {
            writer.push_back(x.Variable);
        },
    }, term);
}

void TCommands::TInliner::InlineTransformation(
    const NCommands::TSyntax::TTransformation& xfm,
    NCommands::TSyntax::TTransformation& writer
) {
    ++Depth;
    Y_DEFER {--Depth;};
    CheckDepth();
    writer.Mods.reserve(xfm.Mods.size());
    for (auto& mod : xfm.Mods) {
        writer.Mods.push_back({mod.Function, {}});
        auto& newMod = writer.Mods.back();
        newMod.Arguments.reserve(mod.Arguments.size());
        for (auto& arg : mod.Arguments) {
            auto& newArg = newMod.Arguments.emplace_back();
            newArg.reserve(arg.size());
            for (auto& term : arg) {
                InlineModArgTerm(term, newArg);
            }
        }
    }
    bool bodyInlined = [&](){
        if (xfm.Body.size() == 1 && xfm.Body.front().size() == 1) {
            if (auto var = std::get_if<NPolexpr::EVarId>(&xfm.Body.front().front())) {
                auto def = GetVariableDefinition(*var);
                if (!def.Definition)
                    return false;
                if (def.Definition->Script.size() == 0)
                    return true;
                if (def.Definition->Script.size() != 1)
                    ythrow TError() << "unexpected multicommand in a substitution: " << Commands.Values.GetVarName(*var);
                NCommands::TSyntax newBody;
                TCmdWriter newWriter(newBody, false);
                if (def.LegacyMode != ELegacyMode::None) {
                    Y_ASSERT(LegacyVars.RecursionDepth.contains(*var));
                    ++LegacyVars.RecursionDepth[*var];
                    Y_DEFER {--LegacyVars.RecursionDepth[*var];};
                    InlineCommands(def.Definition->Script, newWriter);
                } else
                    InlineCommands(def.Definition->Script, newWriter);
                if (newBody.Script.size() == 0)
                    return true;
                if (newBody.Script.size() != 1)
                    ythrow TError() << "totally unexpected multicommand in a substitution: " << Commands.Values.GetVarName(*var);
                    writer.Body = std::move(newBody.Script[0]);
                return true;
            }
        }
        return false;
    }();
    if (!bodyInlined)
        writer.Body = xfm.Body;
}

void TCommands::TInliner::InlineScalarTerm(
    const NCommands::TSyntax::TTerm& term,
    TCmdWriter& writer
) {
    ++Depth;
    Y_DEFER {--Depth;};
    CheckDepth();
    std::visit(TOverloaded{
        [&](NPolexpr::TConstId id) {
            writer.WriteTerm(id);
        },
        [&](NPolexpr::EVarId var) {
            auto def = GetVariableDefinition(var);
            if (!def.Definition) {
                writer.WriteTerm(var);
                return;
            }
            if (def.Definition->Script.size() == 0)
                return;
            if (def.Definition->Script.size() != 1)
                ythrow TError() << "unexpected multicommand substitution: " << Commands.Values.GetVarName(var);
            if (def.Definition->Script[0].size() == 0)
                return;
            if (def.Definition->Script[0].size() != 1) {
                // TODO just disallow this?
                if (def.LegacyMode == ELegacyMode::None || LegacyVars.RecursionDepth[var] != 0)
                    ythrow TError() << "unexpected multiargument substitution: " << Commands.Values.GetVarName(var);
                // TODO dev warning?
                // leave it for the actual evaluation to expand
                writer.WriteTerm(var);
                return;
            }
            if (def.LegacyMode != ELegacyMode::None) {
                Y_ASSERT(LegacyVars.RecursionDepth.contains(var));
                ++LegacyVars.RecursionDepth[var];
                Y_DEFER {--LegacyVars.RecursionDepth[var];};
                InlineScalarTerms(def.Definition->Script[0][0], writer);
            } else
                InlineScalarTerms(def.Definition->Script[0][0], writer);
        },
        [&](const NCommands::TSyntax::TTransformation& xfm) {
            auto newXfm = NCommands::TSyntax::TTransformation();
            InlineTransformation(xfm, newXfm);
            writer.WriteTerm(std::move(newXfm));
        },
        [&](const NCommands::TSyntax::TCall& call) {
            auto def = GetMacroDefinition(call.Function);
            if (!def)
                ythrow TError() << "unknown macro call";
            TScope scope(Scope);
            FillMacroArgs(call, scope);
            Scope = &scope;
            Y_DEFER {Scope = scope.Base;};
            if (def->Script.size() == 0)
                return;
            if (def->Script.size() != 1)
                ythrow TError() << "unexpected multicommand call: " << Commands.Values.GetVarName(call.Function);
            if (def->Script[0].size() == 0)
                return;
            if (def->Script[0].size() != 1)
                ythrow TError() << "unexpected multiargument call: " << Commands.Values.GetVarName(call.Function);
            InlineScalarTerms(def->Script[0][0], writer);
        },
        [&](const NCommands::TSyntax::TBuiltinIf& call) {
            NCommands::TSyntax condExpanded;
            auto condWriter = TCmdWriter(condExpanded, false);
            condWriter.BeginCommand();
            condWriter.BeginArgument();
            InlineScalarTerm(*call.Cond, condWriter);
            condWriter.EndArgument();
            condWriter.EndCommand();
            if (!(condExpanded.Script.size() == 1 && condExpanded.Script.front().size() == 1 && condExpanded.Script.front().front().size() == 1))
                ythrow TError() << "single-term condition expected";
            auto eval = NCommands::TPreevaluator{Commands.Mods, Commands.Values, LegacyVars.Vars};
            auto cond = eval.EvalTerm(condExpanded.Script.front().front().front());
            auto condBool = std::get_if<bool>(&cond);
            if (!condBool)
                ythrow TError() << "boolean condition expected";
            if (*condBool) {
                if (call.Then.size() != 1)
                    ythrow TError() << "unexpected multiargument evaluation";
                InlineScalarTerms(call.Then[0], writer);
            } else {
                if (call.Else.size() != 1)
                    ythrow TError() << "unexpected multiargument evaluation";
                InlineScalarTerms(call.Else[0], writer);
            }
        },
        [&](const NCommands::TSyntax::TIdOrString&) {
            Y_ABORT();
        },
        [&](const NCommands::TSyntax::TUnexpanded& x) {
            writer.WriteTerm(x);
        },
    }, term);
}

void TCommands::TInliner::InlineScalarTerms(
    const NCommands::TSyntax::TArgument& arg,
    TCmdWriter& writer
) {
    ++Depth;
    Y_DEFER {--Depth;};
    CheckDepth();
    for (auto& term : arg)
        InlineScalarTerm(term, writer);
}

void TCommands::TInliner::InlineArguments(
    const NCommands::TSyntax::TCommand& cmd,
    TCmdWriter& writer
) {
    ++Depth;
    Y_DEFER {--Depth;};
    CheckDepth();
    auto doAVariable = [&](NPolexpr::EVarId id) {
        auto def = GetVariableDefinition(id);
        if (!def.Definition)
            return false;
        if (def.Definition->Script.size() == 0)
            return true;
        if (def.Definition->Script.size() != 1)
            ythrow TError() << "unexpected multicommand substitution: " << Commands.Values.GetVarName(id);
        if (def.LegacyMode != ELegacyMode::None) {
            Y_ASSERT(LegacyVars.RecursionDepth.contains(id));
            ++LegacyVars.RecursionDepth[id];
            Y_DEFER {--LegacyVars.RecursionDepth[id];};
            InlineArguments(def.Definition->Script[0], writer);
        } else
            InlineArguments(def.Definition->Script[0], writer);
        return true;
    };
    auto doACall = [&](const NCommands::TSyntax::TCall& call) {
        auto def = GetMacroDefinition(call.Function);
        if (!def)
            return false;
        TScope scope(Scope);
        FillMacroArgs(call, scope);
        Scope = &scope;
        Y_DEFER {Scope = scope.Base;};
        if (def->Script.size() == 0)
            return true;
        if (def->Script.size() != 1)
            ythrow TError() << "unexpected multicommand call: " << Commands.Values.GetVarName(call.Function);
        InlineArguments(def->Script[0], writer);
        return true;
    };
    auto tryStandalone = [&](const NCommands::TSyntax::TTerm& term) {
        return std::visit(TOverloaded{
            [&](NPolexpr::EVarId var) {
                return doAVariable(var);
            },
            [&](const NCommands::TSyntax::TTransformation& xfm) {
                if (xfm.Mods.empty())
                    if (xfm.Body.size() == 1 && xfm.Body.front().size() == 1)
                        if (auto var = std::get_if<NPolexpr::EVarId>(&xfm.Body.front().front()))
                            if (doAVariable(*var))
                                return true;
                return false;
            },
            [&](const NCommands::TSyntax::TCall& call) {
                return doACall(call);
            },
            [&](const NCommands::TSyntax::TBuiltinIf& call) {
                NCommands::TSyntax condExpanded;
                auto condWriter = TCmdWriter(condExpanded, false);
                condWriter.BeginCommand();
                condWriter.BeginArgument();
                InlineScalarTerm(*call.Cond, condWriter);
                condWriter.EndArgument();
                condWriter.EndCommand();
                if (!(condExpanded.Script.size() == 1 && condExpanded.Script.front().size() == 1 && condExpanded.Script.front().front().size() == 1))
                    ythrow TError() << "single-term condition expected";
                auto eval = NCommands::TPreevaluator{Commands.Mods, Commands.Values, LegacyVars.Vars};
                auto cond = eval.EvalTerm(condExpanded.Script.front().front().front());
                auto condBool = std::get_if<bool>(&cond);
                if (!condBool)
                    ythrow TError() << "boolean condition expected";
                if (*condBool)
                    InlineArguments(call.Then, writer);
                else
                    InlineArguments(call.Else, writer);
                return true;
            },
            [&](auto&&) {
                return false;
            }
        }, term);
    };
    for (auto& arg : cmd) {
        if (arg.size() == 1)
            if (tryStandalone(arg[0]))
                continue;
        writer.BeginArgument();
        InlineScalarTerms(arg, writer);
        writer.EndArgument();
    }
}

void TCommands::TInliner::InlineCommands(
    const NCommands::TSyntax::TScript& scr,
    TCmdWriter& writer
) {
    ++Depth;
    Y_DEFER {--Depth;};
    CheckDepth();
    auto doAVariable = [&](NPolexpr::EVarId id) {
        auto def = GetVariableDefinition(id);
        if (!def.Definition)
            return false;
        if (def.LegacyMode != ELegacyMode::None) {
            Y_ASSERT(LegacyVars.RecursionDepth.contains(id));
            ++LegacyVars.RecursionDepth[id];
            Y_DEFER {--LegacyVars.RecursionDepth[id];};
            InlineCommands(def.Definition->Script, writer);
        } else
            InlineCommands(def.Definition->Script, writer);
        return true;
    };
    auto doACall = [&](const NCommands::TSyntax::TCall& call) {
        auto def = GetMacroDefinition(call.Function);
        if (!def)
            return false;
        TScope scope(Scope);
        FillMacroArgs(call, scope);
        Scope = &scope;
        Y_DEFER {Scope = scope.Base;};
        InlineCommands(def->Script, writer);
        return true;
    };
    auto tryStandalone = [&](const NCommands::TSyntax::TTerm& term) {
        return std::visit(TOverloaded{
            [&](NPolexpr::EVarId var) {
                return doAVariable(var);
            },
            [&](const NCommands::TSyntax::TTransformation& xfm) {
                if (xfm.Mods.empty())
                    if (xfm.Body.size() == 1 && xfm.Body.front().size() == 1)
                        if (auto var = std::get_if<NPolexpr::EVarId>(&xfm.Body.front().front()))
                            if (doAVariable(*var))
                                return true;
                return false;
            },
            [&](const NCommands::TSyntax::TCall& call) {
                return doACall(call);
            },
            [&](const NCommands::TSyntax::TBuiltinIf& call) {
                NCommands::TSyntax condExpanded;
                auto condWriter = TCmdWriter(condExpanded, false);
                condWriter.BeginCommand();
                condWriter.BeginArgument();
                InlineScalarTerm(*call.Cond, condWriter);
                condWriter.EndArgument();
                condWriter.EndCommand();
                if (!(condExpanded.Script.size() == 1 && condExpanded.Script.front().size() == 1 && condExpanded.Script.front().front().size() == 1))
                    ythrow TError() << "single-term condition expected";
                auto eval = NCommands::TPreevaluator{Commands.Mods, Commands.Values, LegacyVars.Vars};
                auto cond = eval.EvalTerm(condExpanded.Script.front().front().front());
                auto condBool = std::get_if<bool>(&cond);
                if (!condBool)
                    ythrow TError() << "boolean condition expected";
                if (*condBool)
                    InlineCommands({call.Then}, writer);
                else
                    InlineCommands({call.Else}, writer);
                return true;
            },
            [&](auto&&) {
                return false;
            }
        }, term);
    };
    for (auto& cmd : scr) {
        if (cmd.size() == 1 && cmd[0].size() == 1)
            if (tryStandalone(cmd[0][0]))
                continue;
        writer.BeginCommand();
        InlineArguments(cmd, writer);
        writer.EndCommand();
    }
}

NCommands::TSyntax TCommands::TInliner::Inline(const NCommands::TSyntax& ast) {
    auto checkRecursionStuff = [&]() {
#ifndef NDEBUG
        for (auto& [k, v] : LegacyVars.RecursionDepth)
            Y_ASSERT(v == 0);
#endif
    };
    auto result = NCommands::TSyntax();
    auto writer = TCmdWriter(result, false);
    checkRecursionStuff();
    InlineCommands(ast.Script, writer);
    checkRecursionStuff();
    return result;
}

void TCommands::TInliner::CheckDepth() {
    if (Depth > 50)
        throw TError() << "inlining too deep";
}

NCommands::TCompiledCommand TCommands::Compile(
    TStringBuf cmd,
    const TBuildConfiguration& conf,
    const TVars& vars,
    bool preevaluate,
    TCompilationIODesc io
) {
    auto inliner = TInliner(conf, *this, vars);
    auto& cachedAst = Parse(conf, Mods, Values, TString(cmd));
    auto ast = inliner.Inline(cachedAst);
    // TODO? VarRecursionDepth.clear(); // or clean up individual items as we go?
    if (preevaluate) {
#if 0
        YDebug() << "preevaluating " << PrintExpr(ast) << Endl;
#endif
        auto result = Preevaluate(ast, vars, io);
#if 0
        YDebug() << "           -> " << PrintCmd(result.Expression) << Endl;
#endif
        return result;
    } else
        return NCommands::TCompiledCommand{.Expression = NCommands::Compile(Mods, ast)};
}

ui32 TCommands::Add(TDepGraph& graph, NPolexpr::TExpression expr) {
    ECmdId id;
    {
        auto key = GoodHash(expr);
        const auto [it, inserted] = Command2Id.insert({key, static_cast<ECmdId>(Commands.size())});
        if (inserted) {
            Commands.push_back(std::move(expr));
        }
        id = it->second;
    }
    const ui32 elemId = graph.Names().AddName(EMNT_BuildCommand, fmt::format("S:{}", static_cast<ui32>(id)));
    Elem2Cmd[elemId] = id;
    return elemId;
}

TString TCommands::PrintExpr(const NCommands::TSyntax& expr) const {
    TStringStream os;
    os << "[";
    for (auto& cmd : expr.Script) {
        if (&cmd != &expr.Script.front())
            os << ", ";
        PrintCmd(cmd, os);
    }
    os << "]";
    return os.Str();
}

TString TCommands::PrintCmd(const NPolexpr::TExpression& cmdExpr, size_t highlightBegin, size_t highlightEnd) const {
    TStringStream dest;
    TString buf;
    NPolexpr::Print(dest, cmdExpr, TOverloaded{
        [&](NPolexpr::TConstId id) {
            buf = PrintConst(id);
            return buf;
        },
        [&](NPolexpr::EVarId id) {
            return Values.GetVarName(id);
        },
        [&](NPolexpr::TFuncId id) {
            buf = ToString(Mods.Id2Func(id));
            return buf;
        }
    }, highlightBegin, highlightEnd);
    return dest.Str();
}

void TCommands::PrintCmd(const NCommands::TSyntax::TCommand& cmd, IOutputStream& os) const {
    os << "[";
    for (auto& arg : cmd) {
        if (&arg != &cmd.front())
            os << ", ";
        for (auto& term : arg) {
            if (&term != &arg.front())
                os << " + ";
            std::visit(TOverloaded{
                [&](NPolexpr::TConstId id) {
                    os << PrintConst(id);
                },
                [&](NPolexpr::EVarId id) {
                    os << Values.GetVarName(id);
                },
                [&](const NCommands::TSyntax::TTransformation& xfm) {
                    os << "{";
                    for (auto& mod : xfm.Mods) {
                        if (&mod != &xfm.Mods.front())
                            os << ", ";
                        os << mod.Function;
                        if (!mod.Arguments.empty())
                            PrintCmd(mod.Arguments, os);
                    }
                    if (!xfm.Mods.empty())
                        os << ": ";
                    PrintCmd(xfm.Body, os);
                    os << "}";
                },
                [&](const NCommands::TSyntax::TCall& call) {
                    os << Values.GetVarName(call.Function) << "(";
                    for (auto& x : call.Arguments) {
                        if (&x != &call.Arguments.front())
                            os << ", ";
                        os << PrintExpr(x);
                    }
                    os << ")";
                },
                [&](const NCommands::TSyntax::TBuiltinIf& call) {
                    os << "IF(";
                    PrintCmd({{*call.Cond}}, os);
                    os << ", ";
                    PrintCmd(call.Then, os);
                    os << ", ";
                    PrintCmd(call.Else, os);
                    os << ")";
                },
                [&](const NCommands::TSyntax::TIdOrString& x) {
                    os << "~" << x.Value;
                },
                [&](const NCommands::TSyntax::TUnexpanded& x) {
                    os << "!" << Values.GetVarName(x.Variable);
                }
            }, term);
        }
    }
    os << "]";
}

TString TCommands::PrintConst(NPolexpr::TConstId id) const {
    // TODO strings here would be better with the {:?s} formatting, but the current fmt is too old for that
    return std::visit(TOverloaded{
        [](std::monostate                           ) { return fmt::format("{{}}"); },
        [](bool                                  val) { return fmt::format("#{}", val ? "t" : "f"); },
        [](std::string_view                      val) { return fmt::format("'{}'", val); },
        [](std::vector<std::string_view>         val) { return fmt::format("'{}'", fmt::join(val, "', '")); },
        [](TMacroValues::TTool                   val) { return fmt::format("Tool{{'{}'}}", val.Data); },
        [](TMacroValues::TTools                  val) { return fmt::format("Tools{{{}}}", fmt::join(val.Data, " ")); },
        [](TMacroValues::TInput                  val) { return fmt::format("Input{{{}}}", val.Coord); },
        [](TMacroValues::TInputs                 val) { return fmt::format("Inputs{{{}}}", fmt::join(val.Coords, " ")); },
        [](TMacroValues::TOutput                 val) { return fmt::format("Output{{{}}}", val.Coord); },
        [](TMacroValues::TOutputs                val) { return fmt::format("Outputs{{{}}}", fmt::join(val.Coords, " ")); },
        [](TMacroValues::TGlobPattern            val) { return fmt::format("GlobPattern{{{}}}", fmt::join(val.Data, " ")); },
        [](TMacroValues::TLegacyLateGlobPatterns val) { return fmt::format("LegacyLateGlobPattern{{{}}}", fmt::join(val.Data, " ")); },
    }, Values.GetValue(id));
}

void TCommands::StreamCmdRepr(
    const NPolexpr::TExpression& cmdExpr,
    std::function<void(const char* data, size_t size)> sink
) const {
    for (auto&& node : cmdExpr.GetNodes()) {
        using EType = NPolexpr::TExpression::TNode::EType;
        auto nodeToHash = node;
        auto nodeStr = TString();
        switch (node.GetType()) {
            // wherever necessary, drop the (unstable) index
            // and replace it with a textual representation of the node
            case EType::Constant: {
                auto val = node.AsConst();
                nodeToHash.Assign(NPolexpr::TConstId{val.GetStorage(), 0});
                nodeStr = PrintRawCmdNode(val);
                break;
            }
            case EType::Variable: {
                nodeToHash.Assign(NPolexpr::EVarId(0));
                nodeStr = PrintRawCmdNode(node.AsVar());
                break;
            }
            default:
                break;
        }
        sink(reinterpret_cast<const char*>(&nodeToHash), sizeof nodeToHash);
        if (!nodeStr.empty())
            sink(nodeStr.data(), nodeStr.size());
    }
}

NCommands::TCompiledCommand TCommands::Preevaluate(NCommands::TSyntax& expr, const TVars& vars, TCompilationIODesc io) {
    NCommands::TCompiledCommand result;
    if (!io.MainOutput.empty())
        result.Outputs.CollectCoord(std::get<std::string_view>(Values.GetValue(Values.InsertStr(io.MainOutput))));
    if (io.KnownInputs)
        for (auto& x : *io.KnownInputs)
            result.Inputs.CollectCoord(std::get<std::string_view>(Values.GetValue(Values.InsertStr(x.Name))));
    if (io.KnownOutputs)
        for (auto& x : *io.KnownOutputs)
            result.Outputs.CollectCoord(std::get<std::string_view>(Values.GetValue(Values.InsertStr(x.Name))));
    auto reducer = NCommands::TRefReducer{Mods, Values, vars, result};
    try {
        reducer.ReduceIf(expr);
    } catch(const std::exception&) {
        // note that this is likely to show a partially preevaluated AST
        YDebug() << "Error while preevaluating " << PrintExpr(expr) << Endl;
        throw;
    }
    return result;
}

void TCommands::WriteShellCmd(
    ICommandSequenceWriter* writer,
    const NPolexpr::TExpression& cmdExpr,
    const TVars& vars,
    const TVector<std::span<TVarStr>>& inputs,
    TCommandInfo& cmd,
    const TCmdConf* cmdConf,
    const TBuildConfiguration& buildConf,
    TErrorShowerState* errorShower
) const {
    NCommands::TScriptEvaluator se(this, cmdConf, buildConf, &vars, &inputs, &cmd);
    writer->BeginScript();
    auto result = se.DoScript(&cmdExpr, 0, errorShower, writer);
    Y_DEBUG_ABORT_UNLESS(result.End == cmdExpr.GetNodes().size());
    writer->EndScript(cmd, vars);
    // TODO? `const TCommandInfo&`
}

void TCommands::Save(TMultiBlobBuilder& builder) const {
    Values.Save(builder);

    TBuffer bufferElems;
    TBufferOutput outputElems(bufferElems);
    TSerializer<decltype(Elem2Cmd)>::Save(&outputElems, Elem2Cmd);
    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(bufferElems)));

    TBuffer bufferCmds;
    TBufferOutput outputCmds(bufferCmds);
    TSerializer<decltype(Commands)>::Save(&outputCmds, Commands);
    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(bufferCmds)));

    TBuffer bufferIds;
    TBufferOutput outputIds(bufferIds);
    TSerializer<decltype(Command2Id)>::Save(&outputIds, Command2Id);
    builder.AddBlob(new TBlobSaverMemory(TBlob::FromBufferSingleThreaded(bufferIds)));
}

void TCommands::Load(const TBlob& multi) {
    TSubBlobs blob(multi);
    if (blob.size() != 4) {
        throw std::runtime_error{"Cannot load TCommands, number of received blobs " + ToString(blob.size()) + " expected 4"};
    }
    Values.Load(blob[0]);

    TMemoryInput inputElems(blob[1].Data(), blob[1].Length());
    TSerializer<decltype(Elem2Cmd)>::Load(&inputElems, Elem2Cmd);

    TMemoryInput inputCmds(blob[2].Data(), blob[2].Length());
    TSerializer<decltype(Commands)>::Load(&inputCmds, Commands);

    TMemoryInput inputIds(blob[3].Data(), blob[3].Length());
    TSerializer<decltype(Command2Id)>::Load(&inputIds, Command2Id);
}

TVector<TStringBuf> TCommands::GetCommandVars(ui32 elemId) const {
    const auto* expr = GetByElemId(elemId);
    if (Y_UNLIKELY(expr == nullptr)) {
        return {};
    }

    TUniqVector<TStringBuf> result;
    for (const auto& node : expr->GetNodes()) {
        if (node.GetType() != NPolexpr::TExpression::TNode::EType::Variable)
            continue;
        auto id = static_cast<NPolexpr::EVarId>(node.GetIdx());
        auto value = Values.GetVarName(id);
        if (!value.empty())
            result.Push(value);
    }
    return result.Take();
}

TVector<TStringBuf> TCommands::GetCommandTools(ui32 elemId) const {
    const auto* expr = GetByElemId(elemId);
    if (Y_UNLIKELY(expr == nullptr)) {
        return {};
    }

    TUniqVector<TStringBuf> result;
    for (const auto& node : expr->GetNodes()) {
        if (node.GetType() != NPolexpr::TExpression::TNode::EType::Constant)
            continue;
        auto id = NPolexpr::TConstId::FromRepr(node.GetIdx());
        switch (id.GetStorage()) {
        case TMacroValues::ST_TOOLS:
            {
                auto tool = std::get<TMacroValues::TTool>(Values.GetValue(id)).Data;
                if (!tool.empty())
                    result.Push(tool);
                break;
            }
        case TMacroValues::ST_TOOL_ARRAYS:
            {
                auto tools = std::get<TMacroValues::TTools>(Values.GetValue(id)).Data;
                for (auto& tool : tools)
                    result.Push(tool);
                break;
            }
        }
    }
    return result.Take();
}

NCommands::TTermValue TCommands::EvalConst(const TMacroValues::TValue& value, const NCommands::TEvalCtx& ctx) const {
    return std::visit(TOverloaded{
        [](std::monostate) {
            return NCommands::TTermValue(NCommands::TTermNothing());
        },
        [](bool val) {
            // TODO do we want proper render-time booleans?
            return NCommands::TTermValue(val ? True : False);
        },
        [](std::string_view val) {
            return NCommands::TTermValue(TString(val));
        },
        [](std::vector<std::string_view> val) {
            TVector<TString> result;
            result.reserve(val.size());
            for (auto& x : val)
                result.push_back(TString(x));
            return NCommands::TTermValue(result);
        },
        [&](TMacroValues::TTool val) {
            if (!ctx.CmdInfo.ToolPaths)
                return NCommands::TTermValue(TString("TODO/unreachable?/tool/") + val.Data);
            return NCommands::TTermValue(ctx.CmdInfo.ToolPaths->at(val.Data));
        },
        [&](TMacroValues::TTools val) {
            if (val.Data.empty())
                return NCommands::TTermValue(NCommands::TTermNothing{});
            if (!ctx.CmdInfo.ToolPaths)
                return NCommands::TTermValue(TString("TODO/unreachable?/tool/") + fmt::format("{}", fmt::join(val.Data, ", ")));
            TVector<TString> result;
            for (auto& tool : val.Data)
                result.push_back(ctx.CmdInfo.ToolPaths->at(tool));
            return NCommands::TTermValue(result);
        },
        [&](TMacroValues::TInput input) {
            return NCommands::TTermValue(InputToStringArray(input, ctx));
        },
        [&](const TMacroValues::TInputs& inputs) {
            auto result = TUniqVector<TString>();
            for (auto& coord : inputs.Coords) {
                auto inputResult = InputToStringArray(TMacroValues::TInput {.Coord = coord}, ctx);
                for (auto& input : inputResult)
                    result.Push(input);
            }
            return NCommands::TTermValue(result.Take());
        },
        [&](TMacroValues::TOutput val) {
            auto& var = ctx.Vars.at("OUTPUT");
            return NCommands::TTermValue(TString(var.at(val.Coord).Name));
        },
        [&](const TMacroValues::TOutputs& outputs) {
            auto result = TVector<TString>();
            auto& var = ctx.Vars.at("OUTPUT");
            for (auto& coord : outputs.Coords) {
                result.push_back(var.at(coord).Name);
            }
            return NCommands::TTermValue(result);
        },
        [](TMacroValues::TGlobPattern val) {
            return NCommands::TTermValue(val.Data);
        },
        [](TMacroValues::TLegacyLateGlobPatterns) -> NCommands::TTermValue {
            ythrow TError() << "unexpected glob evaluation";
        },
    }, value);
}

TVector<TString> TCommands::InputToStringArray(const TMacroValues::TInput& input, const NCommands::TEvalCtx& ctx) const {
    TVector<TString> result;
    for (auto it : ctx.Inputs[input.Coord]) {
        result.push_back(it.Name);
    }
    return result;
}

TString TCommands::PrintRawCmdNode(NPolexpr::TConstId node) const {
    return std::visit(TOverloaded{
        [](std::monostate                           ) { return TString(); },
        [](bool                                  val) { return val ? True : False; },
        [](std::string_view                      val) { return TString(val); },
        [](std::vector<std::string_view>         val) { return JoinArgs(std::span(val), std::identity()); },
        [](TMacroValues::TTool                   val) { return TString(val.Data); },
        [](TMacroValues::TTools                  val) { return TString(fmt::format("{}", fmt::join(val.Data, " "))); },
        [](TMacroValues::TInput                  val) { return TString(fmt::format("{}", val.Coord)); },
        [](TMacroValues::TInputs                 val) { return TString(fmt::format("{}", fmt::join(val.Coords, " "))); },
        [](TMacroValues::TOutput                 val) { return TString(fmt::format("{}", val.Coord)); },
        [](TMacroValues::TOutputs                val) { return TString(fmt::format("{}", fmt::join(val.Coords, " "))); },
        [](TMacroValues::TGlobPattern            val) { return JoinArgs(std::span(val.Data), std::identity()); },
        [](TMacroValues::TLegacyLateGlobPatterns val) { return JoinArgs(std::span(val.Data), std::identity()); },
    }, Values.GetValue(node));
}

TString TCommands::PrintRawCmdNode(NPolexpr::EVarId node) const {
    return TString(Values.GetVarName(node));
}
