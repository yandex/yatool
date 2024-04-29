#include "command_store.h"
#include "add_dep_adaptor.h"
#include "add_dep_adaptor_inline.h"

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/commands/script_evaluator.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/lang/cmd_parser.h>
#include <devtools/ymake/polexpr/evaluate.h>
#include <devtools/ymake/polexpr/reduce_if.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/common/string.h>

#include <fmt/format.h>
#include <fmt/args.h>
#include <util/generic/overloaded.h>
#include <util/string/escape.h>

namespace {

    template <class...> [[maybe_unused]] constexpr std::false_type always_false_v{}; // TODO

    struct TRefReducer {

        using TInputs = TCommands::TCompiledCommand::TInputs;
        using TOutputs = TCommands::TCompiledCommand::TOutputs;

        TRefReducer(
            TMacroValues& values,
            const TVars& vars,
            TInputs& inputs,
            TOutputs& outputs
        )
            : Values(values)
            , Vars(vars)
            , Inputs(inputs)
            , Outputs(outputs)
        {
        }

        bool Condition(NPolexpr::TFuncId func) {
            RootFnIdx = static_cast<EMacroFunctions>(func.GetIdx());
            return RootFnIdx == EMacroFunctions::Tool
                || RootFnIdx == EMacroFunctions::Input
                || RootFnIdx == EMacroFunctions::Output
                || RootFnIdx == EMacroFunctions::NoAutoSrc;
        };

        TMacroValues::TValue Evaluate(NPolexpr::EVarId id) {
            // TBD what about vector vars here?
            auto name = Values.GetVarName(id);
            auto var = Vars.Lookup(name);
            if (!var || var->empty()) {
                // this is a Very Special Case that is supposed to handle things like `${input:FOOBAR}` / `FOOBAR=$ARCADIA_ROOT/foobar`;
                // note that the extra braces in the result are significant:
                // the pattern should match whatever `TPathResolver::ResolveAsKnown` may expect to see
                auto result = TString(fmt::format("${{{}}}", name));
                return Values.GetValue(Values.InsertStr(result));
            }
            return Eval1(var);
        }

        TMacroValues::TValue Evaluate(NPolexpr::TFuncId id, std::span<NPolexpr::TConstId> args) {

            auto fnIdx = static_cast<EMacroFunctions>(id.GetIdx());

            auto processInput = [this](std::string_view arg0, bool isGlob) -> TMacroValues::TValue {
                auto names = SplitArgs(TString(arg0));
                if (names.size() == 1) {
                    // one does not simply reuse the original argument,
                    // for it might have been transformed (e.g., dequoted)
                    auto pooledName = std::get<std::string_view>(Values.GetValue(Values.InsertStr(names.front())));
                    auto input = TMacroValues::TInput {.Coord = CollectCoord(pooledName, Inputs)};
                    UpdateCoord(Inputs, input.Coord, [&isGlob](auto& var) { var.IsGlob = isGlob; });
                    return input;
                }
                auto result = TMacroValues::TInputs();
                for (auto& name : names) {
                    auto pooledName = std::get<std::string_view>(Values.GetValue(Values.InsertStr(name)));
                    result.Coords.push_back(CollectCoord(pooledName, Inputs));
                    UpdateCoord(Inputs, result.Coords.back(), [&isGlob](auto& var) { var.IsGlob = isGlob; });
                }
                return result;
            };

            if (
                fnIdx == EMacroFunctions::Args ||
                fnIdx == EMacroFunctions::Terms ||
                fnIdx == EMacroFunctions::Tool ||
                fnIdx == EMacroFunctions::Input ||
                fnIdx == EMacroFunctions::Output ||
                fnIdx == EMacroFunctions::Suf ||
                fnIdx == EMacroFunctions::Cat ||
                fnIdx == EMacroFunctions::NoAutoSrc ||
                fnIdx == EMacroFunctions::Glob
            ) {

                TVector<TMacroValues::TValue> unwrappedArgs;
                unwrappedArgs.reserve(args.size());
                for(auto&& arg : args)
                    unwrappedArgs.push_back(Values.GetValue(arg));

                // TODO: get rid of escaping in Args followed by unescaping in Input/Output

                switch (fnIdx) {
                    case EMacroFunctions::Args: {
                        auto result = TString();
                        bool first = true;
                        for (auto& arg : unwrappedArgs) {
                            if (!first)
                                result += " ";
                            result += "\"";
                            EscapeC(std::get<std::string_view>(arg), result);
                            result += "\"";
                            first = false;
                        }
                        return Values.GetValue(Values.InsertStr(result));
                    }
                    case EMacroFunctions::Terms: {
                        auto result = TString();
                        for (auto& arg : unwrappedArgs)
                            result += std::get<std::string_view>(arg);
                        return Values.GetValue(Values.InsertStr(result));
                    }
                    case EMacroFunctions::Tool: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto names = SplitArgs(TString(arg0));
                        if (names.size() == 1) {
                            // one does not simply reuse the original argument,
                            // for it might have been transformed (e.g., dequoted)
                            auto pooledName = std::get<std::string_view>(Values.GetValue(Values.InsertStr(names.front())));
                            return TMacroValues::TTool {.Data = pooledName};
                        }
                        throw std::runtime_error{"Tool arrays are not supported"};
                    }
                    case EMacroFunctions::Input: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto glob = std::get_if<TMacroValues::TGlobPattern>(&unwrappedArgs[0]);
                        if (glob) {
                            return processInput(glob->Data, true);
                        }
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        return processInput(arg0, false);
                    }
                    case EMacroFunctions::Output: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto names = SplitArgs(TString(arg0));
                        if (names.size() == 1) {
                            // one does not simply reuse the original argument,
                            // for it might have been transformed (e.g., dequoted)
                            auto pooledName = std::get<std::string_view>(Values.GetValue(Values.InsertStr(names.front())));
                            return TMacroValues::TOutput {.Coord = CollectCoord(pooledName, Outputs)};
                        }
                        throw std::runtime_error{"Output arrays are not supported"};
                    }
                    case EMacroFunctions::Suf: {
                        if (unwrappedArgs.size() != 2)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto arg1 = std::get<std::string_view>(unwrappedArgs[1]);
                        auto id = Values.InsertStr(fmt::format("{}{}", arg1, arg0));
                        return Values.GetValue(id);
                    }
                    case EMacroFunctions::Cat: {
                        if (unwrappedArgs.size() < 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto cat = TString();
                        for (auto&& a : unwrappedArgs)
                            cat += std::get<std::string_view>(a);
                        auto id = Values.InsertStr(cat);
                        return Values.GetValue(id);
                    }
                    case EMacroFunctions::NoAutoSrc: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get_if<TMacroValues::TOutput>(&unwrappedArgs[0]);
                        if (!arg0)
                            throw TConfigurationError() << "Modifier [[bad]]" << ToString(fnIdx) << "[[rst]] must be applied to a valid output";
                        UpdateCoord(Outputs, arg0->Coord, [](auto& var) {
                            var.NoAutoSrc = true;
                        });
                        return *arg0;
                    }
                    case EMacroFunctions::Glob: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        return TMacroValues::TGlobPattern{ .Data = arg0 };
                    }
                    default:
                        break; // unreachable
                }

            }

            throw TConfigurationError()
                << "Cannot process modifier [[bad]]" << ToString(fnIdx) << "[[rst]]"
                << " while preevaluating [[bad]]" << ToString(RootFnIdx) << "[[rst]]";

        }

        NPolexpr::TConstId Wrap(TMacroValues::TValue value) {
            return Values.InsertValue(std::move(value));
        }

    private:

        template<typename TLinks>
        ui32 CollectCoord(TStringBuf s, TLinks& links) {
            return links.Push(s).first + links.Base;
        }

        template<typename TLinks, typename FUpdater>
        void UpdateCoord(TLinks& links, ui32 coord, FUpdater upd) {
            links.Update(coord - links.Base, upd);
        }

    private:

        TMacroValues& Values;
        const TVars& Vars;
        TInputs& Inputs;
        TOutputs& Outputs;
        EMacroFunctions RootFnIdx;

    };


}

struct TCounterBump {
    explicit TCounterBump(size_t& counter): Counter(counter) { ++Counter; }
    ~TCounterBump() { --Counter; }
    private:
        size_t& Counter;
};

static inline ui64 GoodHash(const NPolexpr::TExpression& expr) noexcept {
    auto bytes = std::as_bytes(expr.GetNodes());
    return CityHash64(reinterpret_cast<const char*>(bytes.data()), bytes.size());
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

const NCommands::TSyntax& TCommands::Parse(TMacroValues& values, TString src) {
    auto result = ParserCache.find(src);
    if (result == ParserCache.end())
        result = ParserCache.emplace(src, NCommands::Parse(values, src)).first;
    return result->second;
}

void TCommands::Premine(const NCommands::TSyntax& ast, const TVars& inlineVars, const TVars& allVars, TMinedVars& newVars) {

    auto processVar = [&](NPolexpr::EVarId id) {

        auto name = Values.GetVarName(id);
        if (name.ends_with("__NOINLINE__") || name.ends_with("__NO_UID__"))
            return;
        auto buildConf = GlobalConf();
        auto blockData = buildConf->BlockData.find(name);
        if (blockData != buildConf->BlockData.end())
            if (blockData->second.IsUserMacro || blockData->second.IsGenericMacro || blockData->second.IsFileGroupMacro)
                return; // TODO how do we _actually_ detect non-vars?

        // do not expand vars that have been overridden
        // (e.g., ignore the global `SRCFLAGS=` from `ymake.core.conf`
        // while dealing with the `SRCFLAGS` parameter in `_SRC()`)
        for (auto macroVars = &allVars; macroVars; macroVars = macroVars->Base) {
            if (macroVars == &inlineVars)
                break;
            if (macroVars->Contains(name))
                return;
        }

        auto var = inlineVars.Lookup(name);
        if (!var || var->DontExpand)
            return;
        Y_ASSERT(!var->BaseVal || !var->IsReservedName); // TODO find out is this is so
        if (buildConf->CommandConf.IsReservedName(name))
            return;

        auto depth = VarRecursionDepth.try_emplace(name, 0).first;
        for (size_t i = 0; i != depth->second; ++i) {
            var = var->BaseVal;
            if (!var || var->DontExpand || var->IsReservedName)
                ythrow TError() << "self-contradictory variable definition (" << name << ")";
        }
        auto newVar = newVars.find(name);
        if (newVar != newVars.end() && newVar->second.size() > depth->second)
            return; // already processed

        if (var->size() != 1)
            ythrow TNotImplemented() << "unexpected variable size " << var->size() << " (" << name << ")";

        auto& val = var->at(0);
        if (!val.HasPrefix)
            ythrow TError() << "unexpected variable format";

        if (newVar == newVars.end())
            newVar = newVars.try_emplace(name).first;
        Y_ASSERT(newVar->second.size() == depth->second);

        ui64 scopeId;
        TStringBuf cmdName;
        TStringBuf cmdValue;
        ParseLegacyCommandOrSubst(val.Name, scopeId, cmdName, cmdValue);
        newVar->second.push_back(MakeHolder<NCommands::TSyntax>(Parse(Values, TString(cmdValue))));

        auto bump = TCounterBump(depth->second);
        Premine(*newVar->second.back(), inlineVars, allVars, newVars);
    };

    for (auto& cmd : ast.Commands)
        for (auto& arg : cmd)
            for (auto& term : arg)
                std::visit(
                    TOverloaded{
                        [&](NPolexpr::TConstId) {
                        },
                        [&](NPolexpr::EVarId id) {
                            processVar(id);
                        },
                        [&](const NCommands::TSyntax::TSubstitution& sub) {
                            for (auto& mod : sub.Mods)
                                for (auto& val : mod.Values)
                                    for (auto& term : val)
                                        if (auto var = std::get_if<NPolexpr::EVarId>(&term); var)
                                            processVar(*var);
                            for (auto& subArg : sub.Body)
                                for (auto& subTerm : subArg)
                                    if (auto var = std::get_if<NPolexpr::EVarId>(&subTerm); var)
                                        processVar(*var);
                        },
                    },
                    term
                );
}

struct TCommands::TCmdWriter {
    // TODO .reserve hints
    TCmdWriter(NCommands::TSyntax& s): S(s) {}
    void BeginCommand()  { S.Commands.emplace_back(); }
    void BeginArgument() { S.Commands.back().emplace_back(); }
    template<typename T>
    void WriteTerm(T t)  { S.Commands.back().back().emplace_back(std::move(t)); }
    void EndArgument()   { if (S.Commands.back().back().empty()) S.Commands.back().pop_back(); }
    void EndCommand()    { if (S.Commands.back().empty()) S.Commands.pop_back(); }
private:
    NCommands::TSyntax& S;
};

void TCommands::InlineModValueTerm(
    const NCommands::TSyntax::TSubstitution::TModifier::TValueTerm& term,
    const TMinedVars& vars,
    NCommands::TSyntax::TSubstitution::TModifier::TValue& writer
) {
    std::visit(TOverloaded{
        [&](NPolexpr::TConstId id) {
            writer.push_back(id);
        },
        [&](NPolexpr::EVarId id) {
                auto name = Values.GetVarName(id);
                auto thatVar = vars.find(name);
                if (thatVar == vars.end()) {
                    writer.push_back(id);
                    return;
                }
                auto depth = VarRecursionDepth.try_emplace(name, 0).first;
                auto& thatDef = *thatVar->second[depth->second];
                if (thatDef.Commands.size() == 0)
                    return;
                if (thatDef.Commands.size() != 1)
                    ythrow TError() << "unexpected multicommand substitution";
                if (thatDef.Commands[0].size() == 0)
                    return;
                if (thatDef.Commands[0].size() != 1) {
                    if (depth->second == 0) {
                        // leave it for the actual evaluation to expand
                        writer.push_back(id);
                        return;
                    }
                    ythrow TError() << "unexpected multiargument substitution";
                }
                for (auto&& thatTerm : thatDef.Commands[0][0]) {
                    std::visit(TOverloaded{
                        [&](NPolexpr::TConstId id) {
                            auto bump = TCounterBump(depth->second);
                            InlineModValueTerm(id, vars, writer);
                        },
                        [&](NPolexpr::EVarId id) {
                            auto bump = TCounterBump(depth->second);
                            InlineModValueTerm(id, vars, writer);
                        },
                        [&](const NCommands::TSyntax::TSubstitution&) {
                            ythrow TError() << "cannot sub-substitute";
                        },
                    }, thatTerm);
                }
        },
    }, term);
}

void TCommands::InlineScalarTerms(
    const NCommands::TSyntax::TArgument& arg,
    const TMinedVars& vars,
    TCmdWriter& writer
) {
    for (auto& term : arg) {
        std::visit(TOverloaded{
            [&](NPolexpr::TConstId id) {
                writer.WriteTerm(id);
            },
            [&](NPolexpr::EVarId var) {
                auto name = Values.GetVarName(var);
                auto thatVar = vars.find(name);
                if (thatVar == vars.end()) {
                    writer.WriteTerm(var);
                    return;
                }
                auto depth = VarRecursionDepth.try_emplace(name, 0).first;
                auto& thatDef = *thatVar->second[depth->second];
                if (thatDef.Commands.size() == 0)
                    return;
                if (thatDef.Commands.size() != 1)
                    ythrow TError() << "unexpected multicommand substitution";
                if (thatDef.Commands[0].size() == 0)
                    return;
                if (thatDef.Commands[0].size() != 1) {
                    if (depth->second == 0) {
                        // leave it for the actual evaluation to expand
                        writer.WriteTerm(var);
                        return;
                    }
                    ythrow TError() << "unexpected multiargument substitution";
                }
                auto bump = TCounterBump(depth->second);
                InlineScalarTerms(thatDef.Commands[0][0], vars, writer);
            },
            [&](const NCommands::TSyntax::TSubstitution& sub) {
                auto newSub = NCommands::TSyntax::TSubstitution();
                newSub.Mods.reserve(sub.Mods.size());
                for (auto& mod : sub.Mods) {
                    newSub.Mods.push_back({mod.Name, {}});
                    auto& newMod = newSub.Mods.back();
                    newMod.Values.reserve(mod.Values.size());
                    for (auto& val : mod.Values) {
                        auto& newVal = newMod.Values.emplace_back();
                        newVal.reserve(val.size());
                        for (auto& term : val) {
                            InlineModValueTerm(term, vars, newVal);
                        }
                    }
                }
                bool bodyInlined = [&](){
                    if (sub.Body.size() == 1 && sub.Body.front().size() == 1) {
                        if (auto var = std::get_if<NPolexpr::EVarId>(&sub.Body.front().front()); var) {
                            auto name = Values.GetVarName(*var);
                            auto thatVar = vars.find(name);
                            if (thatVar == vars.end())
                                return false;
                            auto depth = VarRecursionDepth.try_emplace(name, 0).first;
                            auto& thatDef = *thatVar->second[depth->second];
                            if (thatDef.Commands.size() == 0)
                                return true;
                            if (thatDef.Commands.size() != 1)
                                ythrow TError() << "unexpected multicommand in a substitution";
                            NCommands::TSyntax newBody;
                            TCmdWriter newWriter(newBody);
                            auto bump = TCounterBump(depth->second);
                            InlineCommands(thatDef.Commands, vars, newWriter);
                            if (newBody.Commands.size() != 1)
                                ythrow TError() << "totally unexpected multicommand in a substitution";
                            newSub.Body = std::move(newBody.Commands[0]);
                            return true;
                        }
                    }
                    return false;
                }();
                if (!bodyInlined)
                    newSub.Body = sub.Body;
                writer.WriteTerm(std::move(newSub));
            },
        }, term);
    }
}

void TCommands::InlineArguments(
    const NCommands::TSyntax::TCommand& cmd,
    const TMinedVars& vars,
    TCmdWriter& writer
) {
    auto doAVariable = [&](NPolexpr::EVarId id) {
        auto name = Values.GetVarName(id);
        auto thatVar = vars.find(name);
        if (thatVar == vars.end())
            return false;
        auto depth = VarRecursionDepth.try_emplace(name, 0).first;
        auto& thatDef = *thatVar->second[depth->second];
        if (thatDef.Commands.size() == 0)
            return true;
        if (thatDef.Commands.size() != 1)
            ythrow TError() << "unexpected multicommand substitution";
        auto bump = TCounterBump(depth->second);
        InlineArguments(thatDef.Commands[0], vars, writer);
        return true;
    };
    for (auto& arg : cmd) {
        if (arg.size() == 1) {
            if (auto var = std::get_if<NPolexpr::EVarId>(&arg[0]); var)
                if (doAVariable(*var))
                    continue;
            if (auto sub = std::get_if<NCommands::TSyntax::TSubstitution>(&arg[0]); sub)
                if (sub->Mods.empty())
                    if (sub->Body.size() == 1 && sub->Body.front().size() == 1)
                        if (auto var = std::get_if<NPolexpr::EVarId>(&sub->Body.front().front()); var)
                            if (doAVariable(*var))
                                continue;
        }
        writer.BeginArgument();
        InlineScalarTerms(arg, vars, writer);
        writer.EndArgument();
    }
}

void TCommands::InlineCommands(
    const NCommands::TSyntax::TCommands& cmds,
    const TMinedVars& vars,
    TCmdWriter& writer
) {
    auto doAVariable = [&](NPolexpr::EVarId id) {
        auto name = Values.GetVarName(id);
        auto thatVar = vars.find(name);
        if (thatVar == vars.end())
            return false;
        auto depth = VarRecursionDepth.try_emplace(name, 0).first;
        auto& thatDef = *thatVar->second[depth->second];
        auto bump = TCounterBump(depth->second);
        InlineCommands(thatDef.Commands, vars, writer);
        return true;
    };
    for (auto& cmd : cmds) {
        if (cmd.size() == 1 && cmd[0].size() == 1) {
            if (auto var = std::get_if<NPolexpr::EVarId>(&cmd[0][0]); var)
                if (doAVariable(*var))
                    continue;
            if (auto sub = std::get_if<NCommands::TSyntax::TSubstitution>(&cmd[0][0]); sub)
                if (sub->Mods.empty())
                    if (sub->Body.size() == 1 && sub->Body.front().size() == 1)
                        if (auto var = std::get_if<NPolexpr::EVarId>(&sub->Body.front().front()); var)
                            if (doAVariable(*var))
                                continue;
        }
        writer.BeginCommand();
        InlineArguments(cmd, vars, writer);
        writer.EndCommand();
    }
}

NCommands::TSyntax TCommands::Inline(const NCommands::TSyntax& ast, const TMinedVars& vars) {
    auto result = NCommands::TSyntax();
    auto writer = TCmdWriter(result);
    InlineCommands(ast.Commands, vars, writer);
    return result;
}

TCommands::TCompiledCommand TCommands::Compile(TStringBuf cmd, const TVars& inlineVars, const TVars& allVars, bool preevaluate, EOutputAccountingMode oam) {
    auto checkRecursionStuff = [&]() {
#ifndef NDEBUG
        for (auto& [k, v] : VarRecursionDepth)
            Y_ASSERT(v == 0);
#endif
    };
    auto& cachedAst = Parse(Values, TString(cmd));
    auto newVars = TMinedVars();
    Premine(cachedAst, inlineVars, allVars, newVars);
    checkRecursionStuff();
    auto ast = Inline(cachedAst, newVars);
    checkRecursionStuff();
    // TODO? VarRecursionDepth.clear(); // or clean up individual items as we go?
    auto expr = NCommands::Compile(Values, ast);
    if (preevaluate)
        return Preevaluate(expr, allVars, oam);
    else
        return TCompiledCommand{.Expression = std::move(expr)};
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

TString TCommands::PrintCmd(const NPolexpr::TExpression& cmdExpr) const {
    TStringStream dest;
    TString buf;
    NPolexpr::Print(dest, cmdExpr, TOverloaded{
        [&](NPolexpr::TConstId id) {
            buf = std::visit(TOverloaded{
                [](std::string_view             val) { return fmt::format("'{}'", val); },
                [](TMacroValues::TTool          val) { return fmt::format("Tool{{'{}'}}", val.Data); },
                [](TMacroValues::TInput         val) { return fmt::format("Input{{{}}}", val.Coord); },
                [](TMacroValues::TInputs        val) { return fmt::format("Inputs{{{}}}", fmt::join(val.Coords, " ")); },
                [](TMacroValues::TOutput        val) { return fmt::format("Output{{{}}}", val.Coord); },
                [](TMacroValues::TCmdPattern    val) { return fmt::format("'{}'", val.Data); },
                [](TMacroValues::TGlobPattern   val) { return fmt::format("GlobPattern{{{}}}", val.Data); }
            }, Values.GetValue(id));
            return buf;
        },
        [&](NPolexpr::EVarId id) {
            return Values.GetVarName(id);
        },
        [&](NPolexpr::TFuncId id) {
            buf = ToString(Values.Id2Func(id));
            return buf;
        }
    });
    return dest.Str();
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
        if (!nodeStr.Empty())
            sink(nodeStr.data(), nodeStr.size());
    }
}

TCommands::TCompiledCommand TCommands::Preevaluate(const NPolexpr::TExpression& expr, const TVars& vars, EOutputAccountingMode oam) {
    TCompiledCommand result;
    switch (oam) {
        case EOutputAccountingMode::Default:
            break;
        case EOutputAccountingMode::Module:
            // the zeroth entry is the main output, see TMakeCommand::MineInputsAndOutputs
            result.Outputs.Base = 1;
            break;
    }
    auto reducer = TRefReducer{Values, vars, result.Inputs, result.Outputs};
    result.Expression = NPolexpr::ReduceIf<TMacroValues::TValue>(expr, std::move(reducer));
    return result;
}

void TCommands::WriteShellCmd(
    ICommandSequenceWriter* writer,
    const NPolexpr::TExpression& cmdExpr,
    const TVars& vars,
    const TVector<std::span<TVarStr>>& inputs,
    TCommandInfo& cmd,
    const TCmdConf* cmdConf
) const {
    NCommands::TScriptEvaluator se{this, cmdConf, &vars, &inputs, &cmd};
    writer->BeginScript();
    se.DoScript(&cmdExpr, 0, writer);
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
        if(id.GetStorage() != TMacroValues::ST_TOOLS)
            continue;
        auto value = std::get<TMacroValues::TTool>(Values.GetValue(id)).Data;
        if (!value.empty())
            result.Push(value);
    }
    return result.Take();
}

TString TCommands::ConstToString(const TMacroValues::TValue& value, const NCommands::TEvalCtx& ctx) const {
    return std::visit(TOverloaded{
        [](std::string_view val) {
             return TString(val);
        },
        [&](TMacroValues::TTool val) {
            if (!ctx.CmdInfo.ToolPaths)
                return TString("TODO/unreachable?/tool/") + val.Data;
            return ctx.CmdInfo.ToolPaths->at(val.Data);
        },
        [&](TMacroValues::TInput) -> TString {
            Y_ABORT();
        },
        [&](const TMacroValues::TInputs&) -> TString {
            Y_ABORT();
        },
        [&](TMacroValues::TOutput val) {
            return TString(ctx.Vars.at("OUTPUT").at(val.Coord).Name);
        },
        [](TMacroValues::TCmdPattern val) {
            return TString(val.Data);
        },
        [](TMacroValues::TGlobPattern val) {
            return TString(val.Data);
        }
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
        [](std::string_view           val) { return TString(val); },
        [](TMacroValues::TTool        val) { return TString(val.Data); },
        [](TMacroValues::TInput       val) { return TString(fmt::format("{}", val.Coord)); },
        [](TMacroValues::TInputs      val) { return TString(fmt::format("{}", fmt::join(val.Coords, " "))); },
        [](TMacroValues::TOutput      val) { return TString(fmt::format("{}", val.Coord)); },
        [](TMacroValues::TCmdPattern  val) { return TString(val.Data); },
        [](TMacroValues::TGlobPattern val) { return TString(val.Data); }
    }, Values.GetValue(node));
}

TString TCommands::PrintRawCmdNode(NPolexpr::EVarId node) const {
    return TString(Values.GetVarName(node));
}
