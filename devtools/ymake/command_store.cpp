#include "command_store.h"
#include "add_dep_adaptor.h"
#include "add_dep_adaptor_inline.h"

#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/lang/cmd_parser.h>
#include <devtools/ymake/polexpr/evaluate.h>
#include <devtools/ymake/polexpr/reduce_if.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/common/string.h>

#include <fmt/format.h>
#include <fmt/args.h>
#include <util/generic/overloaded.h>

struct TCommands::TEvalCtx {
    const TVars& Vars;
    TCommandInfo& CmdInfo;
};

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
            auto fnIdx = static_cast<EMacroFunctions>(func.GetIdx());
            return fnIdx == EMacroFunctions::Tool
                || fnIdx == EMacroFunctions::Input
                || fnIdx == EMacroFunctions::Output
                || fnIdx == EMacroFunctions::NoAutoSrc;
        };

        TMacroValues::TValue Evaluate(NPolexpr::EVarId id) {
            // TBD what about vector vars here?
            return Vars.EvalValue(Values.GetVarName(id));
        }

        TMacroValues::TValue Evaluate(NPolexpr::TFuncId id, std::span<NPolexpr::TConstId> args) {

            auto fnIdx = static_cast<EMacroFunctions>(id.GetIdx());

            if (
                fnIdx == EMacroFunctions::Tool ||
                fnIdx == EMacroFunctions::Input ||
                fnIdx == EMacroFunctions::Output ||
                fnIdx == EMacroFunctions::Suf ||
                fnIdx == EMacroFunctions::Cat ||
                fnIdx == EMacroFunctions::NoAutoSrc
            ) {

                TVector<TMacroValues::TValue> unwrappedArgs;
                unwrappedArgs.reserve(args.size());
                for(auto&& arg : args)
                    unwrappedArgs.push_back(Values.GetValue(arg));

                if (unwrappedArgs.size() < 1)
                    throw std::runtime_error{"Invalid number of arguments"};

                switch (fnIdx) {
                    case EMacroFunctions::Tool: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        return TMacroValues::TTool {.Data = arg0};
                    }
                    case EMacroFunctions::Input: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        return TMacroValues::TInput {.Coord = CollectCoord(arg0, Inputs)};
                    }
                    case EMacroFunctions::Output: {
                        if (unwrappedArgs.size() != 1)
                            throw std::runtime_error{"Invalid number of arguments"};
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        return TMacroValues::TOutput {.Coord = CollectCoord(arg0, Outputs)};
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
                        auto arg0 = std::get<TMacroValues::TOutput>(unwrappedArgs[0]);
                        UpdateCoord(Outputs, arg0.Coord, [](auto& var) {
                            var.NoAutoSrc = true;
                        });
                        return arg0;
                    }
                    default:
                        break; // unreachable
                }

            }

            throw std::runtime_error{"Unsupported function"};

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

    };

    using TTermValue = std::variant<std::monostate, TString, TVector<TString>>;

    TTermValue RenderClear(std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "Clear requires 1 argument";
        }
        return std::visit(TOverloaded{
            [](std::monostate) -> TTermValue {
                return std::monostate();
            },
            [&](const TString&) -> TTermValue {
                return TString();
            },
            [&](const TVector<TString>& v) -> TTermValue {
                if (v.empty())
                    return std::monostate();
                return TString();
            }
        }, args[0]);
    }

    TTermValue RenderPre(std::span<const TTermValue> args) {
        if (args.size() != 2) {
            throw yexception() << "Pre requires 2 arguments";
        }
        return std::visit(TOverloaded{

            [](std::monostate) -> TTermValue {
                return std::monostate();
            },

            [&](const TString& body) {
                return std::visit(TOverloaded{
                    [](std::monostate) -> TTermValue {
                        ythrow TNotImplemented() << "Unexpected empty prefix";
                    },
                    [&](const TString& prefix) -> TTermValue {
                        if (prefix.EndsWith(' ')) {
                            auto trimmedPrefix = prefix.substr(0, 1 + prefix.find_last_not_of(' '));
                            return TVector<TString>{std::move(trimmedPrefix), body};
                        }
                        return prefix + body;
                    },
                    [&](const TVector<TString>& prefixes) -> TTermValue {
                        TVector<TString> result;
                        result.reserve(prefixes.size());
                        for (auto& prefix : prefixes)
                            result.push_back(prefix + body);
                        return std::move(result);
                    }
                }, args[0]);
            },

            [&](const TVector<TString>& bodies) -> TTermValue {
                return std::visit(TOverloaded{
                    [](std::monostate) -> TTermValue {
                        ythrow TNotImplemented() << "Unexpected empty prefix";
                    },
                    [&](const TString& prefix) -> TTermValue {
                        TVector<TString> result;
                        if (prefix.EndsWith(' ')) {
                            auto trimmedPrefix = prefix.substr(0, 1 + prefix.find_last_not_of(' '));
                            result.reserve(bodies.size() * 2);
                            for (auto& body : bodies) {
                                result.push_back(trimmedPrefix);
                                result.push_back(body);
                            }
                        } else {
                            result.reserve(bodies.size());
                            for (auto& body : bodies)
                                result.push_back(prefix + body);
                        }
                        return std::move(result);
                    },
                    [&](const TVector<TString>& prefixes) -> TTermValue {
                        Y_UNUSED(prefixes);
                        ythrow TNotImplemented() << "Pre arguments should not both be arrays";
                    }
                }, args[0]);
            }

        }, args[1]);
    }

    TTermValue RenderSuf(std::span<const TTermValue> args) {
        if (args.size() != 2) {
            throw yexception() << "Suf requires 2 arguments";
        }
        auto suffix = std::get<TString>(args[0]);
        return std::visit(TOverloaded{
            [](std::monostate) -> TTermValue {
                return std::monostate();
            },
            [&](const TString& body) -> TTermValue {
                return body + suffix;
            },
            [&](const TVector<TString>& bodies) -> TTermValue {
                TVector<TString> result;
                result.reserve(bodies.size());
                for (auto&& body : bodies)
                    result.push_back(body + suffix);
                return std::move(result);
            }
        }, args[1]);
    }

    TTermValue RenderQuo(std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "Quo requires 1 argument";
        }
        // "quo" is used to wrap pieces of the unparsed command line;
        // the quotes in question should disappear after argument extraction,
        // so for the arg-centric model this modifier is effectively a no-op
        return args[0];
    }

    void RenderEnv(ICommandSequenceWriter* writer, const TCommands::TEvalCtx& ctx, std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "Env requires 1 argument";
        }
        std::visit(TOverloaded{
            [](std::monostate) {
                throw TNotImplemented();
            },
            [&](const TString& s) {
                writer->WriteEnv(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
            },
            [&](const TVector<TString>&) {
                throw TNotImplemented();
            }
        }, args[0]);
    }

    void RenderKeyValue(const TCommands::TEvalCtx& ctx, std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "KeyValue requires 1 argument";
        }
        std::visit(TOverloaded{
            [](std::monostate) {
                throw TNotImplemented();
            },
            [&](const TString& s) {
                // lifted from EMF_KeyValue processing
                TString kvValue = ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false);
                TStringBuf name(kvValue);
                TStringBuf before;
                TStringBuf after;
                if (name.TrySplit(' ', before, after)) {
                    TString val = TString{after};
                    GetOrInit(ctx.CmdInfo.KV)[before] = val;
                } else {
                    GetOrInit(ctx.CmdInfo.KV)[name] = "yes";
                }
            },
            [&](const TVector<TString>&) {
                throw TNotImplemented();
            }
        }, args[0]);
    }

    TTermValue RenderCutExt(std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "Noext requires 1 argument";
        }
        auto apply = [](TString s) {
            // lifted from EMF_CutExt processing:
            size_t slash = s.rfind(NPath::PATH_SEP); //todo: windows slash!
            if (slash == TString::npos)
                slash = 0;
            size_t dot = s.rfind('.');
            if (dot != TString::npos && dot >= slash)
                s = s.substr(0, dot);
            return s;
        };
        return std::visit(TOverloaded{
            [](std::monostate) -> TTermValue {
                throw TNotImplemented();
            },
            [&](TString s) -> TTermValue {
                return apply(std::move(s));
            },
            [&](TVector<TString> v) -> TTermValue {
                for (auto& s : v)
                    s = apply(std::move(s));
                return std::move(v);
            }
        }, args[0]);
    }

    TTermValue RenderLastExt(std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "Lastext requires 1 argument";
        }
        auto apply = [](TString s) {
            // lifted from EMF_LastExt processing:
            // It would be nice to use some common utility function from common/npath.h,
            // but Extension function implements rather strange behaviour
            auto slash = s.rfind(NPath::PATH_SEP);
            auto dot = s.rfind('.');
            if (dot != TStringBuf::npos && (slash == TStringBuf::npos || slash < dot)) {
                s = s.substr(dot + 1);
            } else {
                s.clear();
            }
            return s;
        };
        return std::visit(TOverloaded{
            [](std::monostate) -> TTermValue {
                throw TNotImplemented();
            },
            [&](TString s) -> TTermValue {
                return apply(std::move(s));
            },
            [&](TVector<TString> v) -> TTermValue {
                for (auto& s : v)
                    s = apply(std::move(s));
                return std::move(v);
            }
        }, args[0]);
    }

    TTermValue RenderExtFilter(std::span<const TTermValue> args) {
        if (args.size() != 2) {
            throw yexception() << "Ext requires 2 argument";
        }
        auto ext = std::get<TString>(args[0]);
        return std::visit(TOverloaded{
            [](std::monostate) -> TTermValue {
                throw TNotImplemented();
            },
            [&](TString s) -> TTermValue {
                return s.EndsWith(ext) ? args[1] : std::monostate();
            },
            [&](TVector<TString> v) -> TTermValue {
                v.erase(std::remove_if(v.begin(), v.end(), [&](auto& s) { return !s.EndsWith(ext); }), v.end());
                return std::move(v);
            }
        }, args[1]);
    }

    TTermValue RenderTODO1(std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "TODO1 requires 1 argument";
        }
        auto arg0 = std::visit(TOverloaded{
            [](std::monostate) {
                return TString("-");
            },
            [&](const TString& s) {
                return s;
            },
            [&](const TVector<TString>& v) -> TString {
                return fmt::format("{}", fmt::join(v, " "));
            }
        }, args[0]);
        return fmt::format("TODO1({})", arg0);
    }

    TTermValue RenderTODO2(std::span<const TTermValue> args) {
        if (args.size() != 2) {
            throw yexception() << "TODO2 requires 2 arguments";
        }
        auto arg0 = std::visit(TOverloaded{
            [](std::monostate) {
                return TString("-");
            },
            [&](const TString& s) {
                return s;
            },
            [&](const TVector<TString>& v) -> TString {
                return fmt::format("{}", fmt::join(v, " "));
            }
        }, args[0]);
        auto arg1 = std::visit(TOverloaded{
            [](std::monostate) {
                return TString("-");
            },
            [&](const TString& s) {
                return s;
            },
            [&](const TVector<TString>& v) -> TString {
                return fmt::format("{}", fmt::join(v, " "));
            }
        }, args[1]);
        return fmt::format("TODO2({}, {})", arg0, arg1);
    }

    TTermValue RenderMsvsSource(ICommandSequenceWriter* writer, std::span<const TTermValue> args) {
        if (args.size() != 1) {
            throw yexception() << "MsvsSource requires 1 argument";
        }
        auto arg0 = std::visit(TOverloaded{
            [](std::monostate) -> TString {
                ythrow TNotImplemented();
            },
            [&](const TString& s) {
                return s;
            },
            [&](const TVector<TString>&) -> TString {
                ythrow TNotImplemented();
            }
        }, args[0]);
        writer->RegisterPrimaryInput(arg0);
        return arg0;
    }

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

const NCommands::TSyntax& TCommands::Parse(TMacroValues& values, TString src) {
    auto result = ParserCache.find(src);
    if (result == ParserCache.end())
        result = ParserCache.emplace(src, NCommands::Parse(values, src)).first;
    return result->second;
}

void TCommands::Premine(const NCommands::TSyntax& ast, const TVars& inlineVars, const TVars& allVars, TMinedVars& newVars) {

    auto processVar = [&](NPolexpr::EVarId id) {

        auto name = Values.GetVarName(id);
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
        newVar->second.push_back(Parse(Values, TString(cmdValue)));

        auto bump = TCounterBump(depth->second);
        Premine(newVar->second.back(), inlineVars, allVars, newVars);
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
                            if (auto var = std::get_if<NPolexpr::EVarId>(&sub.Body); var)
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
    void EndArgument()   {}
    void EndCommand()    {}
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
                auto& thatDef = thatVar->second[depth->second];
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
            [&](NPolexpr::EVarId id) {
                auto name = Values.GetVarName(id);
                auto thatVar = vars.find(name);
                if (thatVar == vars.end()) {
                    writer.WriteTerm(id);
                    return;
                }
                auto depth = VarRecursionDepth.try_emplace(name, 0).first;
                auto& thatDef = thatVar->second[depth->second];
                if (thatDef.Commands.size() == 0)
                    return;
                if (thatDef.Commands.size() != 1)
                    ythrow TError() << "unexpected multicommand substitution";
                if (thatDef.Commands[0].size() == 0)
                    return;
                if (thatDef.Commands[0].size() != 1) {
                    if (depth->second == 0) {
                        // leave it for the actual evaluation to expand
                        writer.WriteTerm(id);
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
                newSub.Body = sub.Body; // TODO expand; what do we allow here, anyway?
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
        auto& thatDef = thatVar->second[depth->second];
        if (thatDef.Commands.size() == 0)
            return false;
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
                    if (auto var = std::get_if<NPolexpr::EVarId>(&sub->Body); var)
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
        auto& thatDef = thatVar->second[depth->second];
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
                    if (auto var = std::get_if<NPolexpr::EVarId>(&sub->Body); var)
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

TCommands::TCompiledCommand TCommands::Compile(TStringBuf cmd, const TVars& inlineVars, const TVars& allVars, EOutputAccountingMode oam) {
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
    return Preevaluate(expr, allVars, oam);
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
                [](std::string_view          val) { return fmt::format("'{}'", val); },
                [](TMacroValues::TTool       val) { return fmt::format("Tool{{'{}'}}", val.Data); },
                [](TMacroValues::TInput      val) { return fmt::format("Input{{{}}}", val.Coord); },
                [](TMacroValues::TOutput     val) { return fmt::format("Output{{{}}}", val.Coord); },
                [](TMacroValues::TCmdPattern val) { return fmt::format("'{}'", val.Data); }
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

void TCommands::WriteShellCmd(ICommandSequenceWriter* writer, const NPolexpr::TExpression& cmdExpr, const TVars& vars, TCommandInfo& cmd) const {
    // TODO? `const TCommandInfo&`
    writer->BeginScript();
    TEvalCtx ctx{vars, cmd};
    auto endScr = NPolexpr::VisitFnArgs(cmdExpr, 0, Values.Func2Id(EMacroFunctions::Cmds), [&](size_t beginCmd) {
        writer->BeginCommand();
        auto endCmd = NPolexpr::VisitFnArgs(cmdExpr, beginCmd, Values.Func2Id(EMacroFunctions::Args), [&](size_t beginArg) {
            TVector<TString> args;
            auto endArg = NPolexpr::VisitFnArgs(cmdExpr, beginArg, Values.Func2Id(EMacroFunctions::Terms), [&](size_t beginTerm) {
                auto [term, endTerm] = ::NPolexpr::Evaluate<TTermValue>(cmdExpr, beginTerm, [&](auto id, auto&&... args) -> TTermValue {
                    if constexpr (std::is_same_v<decltype(id), NPolexpr::TConstId>) {
                        static_assert(sizeof...(args) == 0);
                        return TString(ConstToString(Values.GetValue(id), ctx));
                    }
                    else if constexpr (std::is_same_v<decltype(id), NPolexpr::EVarId>) {
                        static_assert(sizeof...(args) == 0);
                        return vars.EvalAllSplit(Values.GetVarName(id));
                    }
                    else if constexpr (std::is_same_v<decltype(id), NPolexpr::TFuncId>) {
                        static_assert(sizeof...(args) == 1);
                        switch (Values.Id2Func(id)) {
                            case EMacroFunctions::Hide: return std::monostate();
                            case EMacroFunctions::Clear: return RenderClear(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::Pre: return RenderPre(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::Suf: return RenderSuf(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::Quo: return RenderQuo(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::SetEnv: RenderEnv(writer, ctx, std::forward<decltype(args)...>(args...)); return {};
                            case EMacroFunctions::CutExt: return RenderCutExt(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::LastExt: return RenderLastExt(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::ExtFilter: return RenderExtFilter(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::KeyValue: RenderKeyValue(ctx, std::forward<decltype(args)...>(args...)); return {};
                            case EMacroFunctions::TODO1: return RenderTODO1(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::TODO2: return RenderTODO2(std::forward<decltype(args)...>(args...));
                            case EMacroFunctions::MsvsSource: return RenderMsvsSource(writer, std::forward<decltype(args)...>(args...));
                            default:
                                break;
                        }
                        throw yexception() << "Don't know how to render configure time modifier " << Values.Id2Func(id) << " in expression: " + PrintCmd(cmdExpr);
                    }
                });

                if (args.empty()) {
                    std::visit(TOverloaded{
                        [&](std::monostate) {
                        },
                        [&](TString&& s) {
                            args.push_back(std::move(s));
                        },
                        [&](TVector<TString>&& v) {
                            args = std::move(v);
                        }
                    }, std::move(term));
                } else {
                    std::visit(TOverloaded{
                        [&](std::monostate) {
                        },
                        [&](TString&& s) {
                            for (auto& arg : args)
                                arg += s;
                        },
                        [&](TVector<TString>&& v) {
                            auto result
                                = fmt::format("{}", fmt::join(args, " "))
                                + fmt::format("{}", fmt::join(v, " "));
                            args = {TString(std::move(result))};
                        }
                    }, std::move(term));
                }

                return endTerm;
            });
            if (endArg == beginArg)
                throw yexception() << "Could not evaluate a command argument";
            for (auto&& arg : args)
                if (!arg.empty())
                    writer->WriteArgument(arg);
            return endArg;
        });
        if (endCmd == beginCmd)
            throw yexception() << "Could not evaluate a command";
        writer->EndCommand();
        return endCmd;
    });
    if (endScr != cmdExpr.GetNodes().size())
        throw yexception() << "Could not evaluate a command sequence";
    writer->EndScript(cmd, vars);
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

TString TCommands::ConstToString(const TMacroValues::TValue& value, const TEvalCtx& ctx) const {
    return std::visit(TOverloaded{
        [](std::string_view val) {
             return TString(val);
        },
        [&](TMacroValues::TTool val) {
            if (!ctx.CmdInfo.ToolPaths)
                return TString("TODO/unreachable?/tool/") + val.Data;
            return ctx.CmdInfo.ToolPaths->at(val.Data);
        },
        [&](TMacroValues::TInput val) {
            return TString(ctx.Vars.at("INPUT").at(val.Coord).Name);
        },
        [&](TMacroValues::TOutput val) {
            return TString(ctx.Vars.at("OUTPUT").at(val.Coord).Name);
        },
        [](TMacroValues::TCmdPattern val) {
            return TString(val.Data);
        }
    }, value);
}

TString TCommands::PrintRawCmdNode(NPolexpr::TConstId node) const {
    return std::visit(TOverloaded{
        [](std::string_view          val) { return TString(val); },
        [](TMacroValues::TTool       val) { return TString(val.Data); },
        [](TMacroValues::TInput      val) { return TString(fmt::format("{}", val.Coord)); },
        [](TMacroValues::TOutput     val) { return TString(fmt::format("{}", val.Coord)); },
        [](TMacroValues::TCmdPattern val) { return TString(val.Data); }
    }, Values.GetValue(node));
}

TString TCommands::PrintRawCmdNode(NPolexpr::EVarId node) const {
    return TString(Values.GetVarName(node));
}
