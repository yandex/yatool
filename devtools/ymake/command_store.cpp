#include "command_store.h"
#include "add_dep_adaptor.h"
#include "add_dep_adaptor_inline.h"

#include <devtools/ymake/command_helpers.h>
#include <devtools/ymake/commands/compilation.h>
#include <devtools/ymake/commands/script_evaluator.h>
#include <devtools/ymake/compact_graph/dep_graph.h>
#include <devtools/ymake/lang/cmd_parser.h>
#include <devtools/ymake/polexpr/evaluate.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/common/string.h>

#include <fmt/format.h>
#include <fmt/args.h>
#include <util/generic/overloaded.h>
#include <util/generic/scope.h>
#include <ranges>

namespace {

    template <class...> [[maybe_unused]] constexpr std::false_type always_false_v{}; // TODO

    struct TRefReducer {

        using TInputs = NCommands::TCompiledCommand::TInputs;
        using TOutputs = NCommands::TCompiledCommand::TOutputs;

        TRefReducer(
            const NCommands::TModRegistry& mods,
            TMacroValues& values,
            const TVars& vars,
            NCommands::TCompiledCommand& sink
        )
            : Mods(mods)
            , Values(values)
            , Vars(vars)
            , Sink(sink)
        {
        }

    public:

        void ReduceIf(NCommands::TSyntax& ast) {
            for (auto& cmd : ast.Script)
                ReduceCmd(cmd);
            Sink.Expression = NCommands::Compile(Mods, ast);
        }

    private:

        void ReduceCmd(NCommands::TSyntax::TCommand& cmd) {
            for (auto& arg : cmd)
                for (auto& term : arg)
                    if (auto xfm = std::get_if<NCommands::TSyntax::TTransformation>(&term)) {
                        auto cutoff = std::find_if(
                            xfm->Mods.begin(), xfm->Mods.end(),
                            [&](auto& mod) {return Condition(Mods.Func2Id(mod.Name));}
                        );
                        if (cutoff == xfm->Mods.end()) {
                            ReduceCmd(xfm->Body);
                            continue;
                        }
                        auto val = EvalCmd(xfm->Body);
                        auto rcutoff = std::make_reverse_iterator(cutoff);
                        for (auto mod = xfm->Mods.rbegin(); mod != rcutoff; ++mod)
                            val = ApplyMod(*mod, val);
                        xfm->Mods.erase(cutoff, xfm->Mods.end());
                        if (xfm->Mods.empty())
                            term = val;
                        else
                            xfm->Body = {{val}};
                    }
        }

        NPolexpr::TConstId EvalCmd(const NCommands::TSyntax::TCommand& cmd) {
            if (cmd.size() == 1 && cmd.front().size() == 1) {
                // `input` modifier shenanigans:
                // * do not add `Args(Terms(...))` wrappers when dealing with `${VAR}`
                //   to avoid extra enquoting of `VAR` contents in, e.g.,
                //   `${input:X}`, `X=${ARCADIA_ROOT}/some/path`
                //   (cf. similar code in `CompileArgs` invoked by `NCommands::Compile`);
                // * _do_ add these wrappers when dealing with `${...:"text"}`,
                //   either direct, or an inlined version of
                //   `${input:X}`, `X="file name with spaces.txt"`,
                //   because the `input` modifier expects a stringified list of enquoted args
                //   (see input processing in `TRefReducer::Evaluate`)
                if (std::holds_alternative<NPolexpr::EVarId>(cmd.front().front()))
                    return EvalTerm(cmd.front().front());
            }
            TVector<NPolexpr::TConstId> args;
            args.reserve(cmd.size());
            for (auto& arg : cmd) {
                TVector<NPolexpr::TConstId> terms;
                terms.reserve(arg.size());
                for (auto& term : arg)
                    terms.push_back(EvalTerm(term));
                args.push_back(Wrap(Evaluate(Mods.Func2Id(EMacroFunction::Terms), std::span(terms))));
            }
            return Wrap(Evaluate(Mods.Func2Id(EMacroFunction::Args), std::span(args)));
        }

        NPolexpr::TConstId EvalTerm(const NCommands::TSyntax::TTerm& term) {
            return std::visit(TOverloaded{
                [&](NPolexpr::TConstId id) {
                    return id;
                },
                [&](NPolexpr::EVarId id) {
                    return Wrap(Evaluate(id));
                },
                [&](const NCommands::TSyntax::TTransformation& xfm) -> NPolexpr::TConstId {
                    auto val = EvalCmd(xfm.Body);
                    for (auto& mod : std::ranges::reverse_view(xfm.Mods))
                        val = ApplyMod(mod, val);
                    return val;
                },
                [&](const NCommands::TSyntax::TCall&) -> NPolexpr::TConstId {
                    Y_ABORT();
                },
                [&](const NCommands::TSyntax::TIdOrString&) -> NPolexpr::TConstId {
                    Y_ABORT();
                },
                [&](const NCommands::TSyntax::TUnexpanded&) -> NPolexpr::TConstId {
                    Y_ABORT();
                }
            }, term);
        }

        NPolexpr::TConstId ApplyMod(const NCommands::TSyntax::TTransformation::TModifier& mod, NPolexpr::TConstId val) {
            TVector<NPolexpr::TConstId> args;
            args.reserve(mod.Values.size() + 1);
            for (auto& modVal : mod.Values) {
                TVector<NPolexpr::TConstId> catArgs;
                catArgs.reserve(modVal.size());
                for (auto& modTerm : modVal) {
                    catArgs.push_back(std::visit(TOverloaded{
                        [&](NPolexpr::TConstId id) {
                            return id;
                        },
                        [&](NPolexpr::EVarId id) {
                            return Wrap(Evaluate(id));
                        },
                    }, modTerm));
                }
                args.push_back(Wrap(Evaluate(Mods.Func2Id(EMacroFunction::Cat), std::span(catArgs))));
            }
            args.push_back(val);
            return Wrap(Evaluate(Mods.Func2Id(mod.Name), std::span(args)));
        }

    private:

        bool Condition(NPolexpr::TFuncId func) {
            RootFnIdx = static_cast<EMacroFunction>(func.GetIdx());
            if (auto desc = Mods.At(RootFnIdx))
                return desc->MustPreevaluate;
            return RootFnIdx == EMacroFunction::Tool
                || RootFnIdx == EMacroFunction::Output
                || RootFnIdx == EMacroFunction::Tmp
                || RootFnIdx == EMacroFunction::Context
                || RootFnIdx == EMacroFunction::NoAutoSrc
                || RootFnIdx == EMacroFunction::NoRel
                || RootFnIdx == EMacroFunction::ResolveToBinDir;
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

            auto fnIdx = static_cast<EMacroFunction>(id.GetIdx());

            if (auto desc = Mods.At(fnIdx); desc && desc->CanPreevaluate) {
                TVector<TMacroValues::TValue> unwrappedArgs;
                unwrappedArgs.reserve(args.size());
                for(auto&& arg : args)
                    unwrappedArgs.push_back(Values.GetValue(arg));
                return desc->Preevaluate({Values, Sink, RootFnIdx}, unwrappedArgs);
            }

            if (
                fnIdx == EMacroFunction::Terms ||
                fnIdx == EMacroFunction::Tool ||
                fnIdx == EMacroFunction::Output ||
                fnIdx == EMacroFunction::Tmp ||
                fnIdx == EMacroFunction::Pre ||
                fnIdx == EMacroFunction::Suf ||
                fnIdx == EMacroFunction::HasDefaultExt ||
                fnIdx == EMacroFunction::CutPath ||
                fnIdx == EMacroFunction::Cat ||
                fnIdx == EMacroFunction::Context ||
                fnIdx == EMacroFunction::NoAutoSrc ||
                fnIdx == EMacroFunction::NoRel ||
                fnIdx == EMacroFunction::ResolveToBinDir ||
                fnIdx == EMacroFunction::Glob
            ) {

                TVector<TMacroValues::TValue> unwrappedArgs;
                unwrappedArgs.reserve(args.size());
                for(auto&& arg : args)
                    unwrappedArgs.push_back(Values.GetValue(arg));
                auto checkArgCount = [&](size_t expected) {
                    if (expected != 0) {
                        if (unwrappedArgs.size() != expected)
                            throw std::runtime_error{fmt::format("Invalid number of arguments in {}, {} expected", ToString(fnIdx), expected)};
                    } else {
                        if (unwrappedArgs.size() == 0)
                            throw std::runtime_error{fmt::format("Missing arguments in {}", ToString(fnIdx))};
                    }
                };
                auto updateOutput = [&](auto fn) {
                    checkArgCount(1);
                    auto arg0 = std::get_if<TMacroValues::TOutput>(&unwrappedArgs[0]);
                    if (!arg0)
                        throw TConfigurationError() << "Modifier [[bad]]" << ToString(fnIdx) << "[[rst]] must be applied to a valid output";
                    UpdateCoord(Sink.Outputs, arg0->Coord, std::move(fn));
                    return *arg0;
                };

                // TODO: get rid of escaping in Args followed by unescaping in Input/Output/CutPath/CutExt

                switch (fnIdx) {
                    case EMacroFunction::Terms: {
                        auto result = TString();
                        for (auto& arg : unwrappedArgs)
                            result += std::get<std::string_view>(arg);
                        return Values.GetValue(Values.InsertStr(result));
                    }
                    case EMacroFunction::Tool: {
                        checkArgCount(1);
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
                    case EMacroFunction::Output:
                    case EMacroFunction::Tmp:
                    {
                        checkArgCount(1);
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto names = SplitArgs(TString(arg0));
                        if (names.size() == 1) {
                            // one does not simply reuse the original argument,
                            // for it might have been transformed (e.g., dequoted)
                            auto pooledName = std::get<std::string_view>(Values.GetValue(Values.InsertStr(names.front())));
                            auto result = TMacroValues::TOutput {.Coord = CollectCoord(pooledName, Sink.Outputs)};
                            if (fnIdx == EMacroFunction::Tmp)
                                UpdateCoord(Sink.Outputs, result.Coord, [](auto& x) {x.IsTmp = true;});
                            return result;
                        }
                        throw std::runtime_error{"Output arrays are not supported"};
                    }
                    case EMacroFunction::Pre: {
                        checkArgCount(2);
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto arg1 = std::get<std::string_view>(unwrappedArgs[1]);
                        auto id = Values.InsertStr(TString::Join(arg0, arg1));
                        return Values.GetValue(id);
                    }
                    case EMacroFunction::Suf:
                    case EMacroFunction::HasDefaultExt:
                    {
                        checkArgCount(2);
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto arg1 = std::get<std::string_view>(unwrappedArgs[1]);
                        if (fnIdx == EMacroFunction::HasDefaultExt) {
                            // cf. EMF_HasDefaultExt handling
                            size_t dot = arg1.rfind('.');
                            size_t slash = arg1.rfind(NPath::PATH_SEP);
                            bool hasSpecExt = slash != TString::npos ? (dot > slash) : true;
                            if (dot != TString::npos && hasSpecExt)
                                return Values.GetValue(Values.InsertStr(arg1));
                        }
                        auto id = Values.InsertStr(TString::Join(arg1, arg0));
                        return Values.GetValue(id);
                    }
                    case EMacroFunction::CutPath: {
                        checkArgCount(1);
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto names = SplitArgs(TString(arg0));
                        if (names.size() != 1) {
                            throw std::runtime_error{"nopath modifier requires a single argument"};
                        }
                        // one does not simply reuse the original argument,
                        // for it might have been transformed (e.g., dequoted)
                        arg0 = names.front();
                        // cf. EMF_CutPath processing
                        size_t slash = arg0.rfind(NPath::PATH_SEP);
                        if (slash != TString::npos)
                            arg0 = arg0.substr(slash + 1);
                        auto id = Values.InsertStr(arg0);
                        return Values.GetValue(id);
                    }
                    case EMacroFunction::Cat: {
                        //checkArgCount(0);
                        auto cat = TString();
                        for (auto&& a : unwrappedArgs)
                            cat += std::get<std::string_view>(a);
                        auto id = Values.InsertStr(cat);
                        return Values.GetValue(id);
                    }
                    case EMacroFunction::Context: {
                        checkArgCount(2);
                        auto arg0 = std::get<std::string_view>(unwrappedArgs[0]);
                        auto context = TFileConf::GetContextType(arg0);
                        if (auto arg1 = std::get_if<TMacroValues::TInputs>(&unwrappedArgs[1])) {
                            for (auto& coord : arg1->Coords)
                                UpdateCoord(Sink.Inputs, coord, [=](auto& var) {
                                    var.Context = context;
                                });
                            return *arg1;
                        }
                        if (auto arg1 = std::get_if<TMacroValues::TInput>(&unwrappedArgs[1])) {
                            UpdateCoord(Sink.Inputs, arg1->Coord, [=](auto& var) {
                                var.Context = context;
                            });
                            return *arg1;
                        }
                        throw TConfigurationError() << "Modifier [[bad]]" << ToString(fnIdx) << "[[rst]] must be applied to a valid input sequence";
                    }
                    case EMacroFunction::NoAutoSrc:
                        return updateOutput([](auto& x) {x.NoAutoSrc = true;});
                    case EMacroFunction::NoRel:
                        return updateOutput([](auto& x) {x.NoRel = true;});
                    case EMacroFunction::ResolveToBinDir:
                        return updateOutput([](auto& x) {x.ResolveToBinDir = true;});
                    case EMacroFunction::Glob: {
                        checkArgCount(1);
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

        const NCommands::TModRegistry& Mods;
        TMacroValues& Values;
        const TVars& Vars;
        NCommands::TCompiledCommand& Sink;
        EMacroFunction RootFnIdx;

    };


}

struct TCommands::TInliner::TScope {
    explicit TScope(const TScope *base): Base(base) {}
public:
    const TScope* Base;
    using TVarDefinitions = THashMap<TStringBuf, NCommands::TSyntax>;
    TVarDefinitions VarDefinitions;
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

const NCommands::TSyntax& TCommands::Parse(const TBuildConfiguration* conf, const NCommands::TModRegistry& mods, TMacroValues& values, TString src) {
    auto result = ParserCache.find(src);
    if (result == ParserCache.end())
        result = ParserCache.emplace(src, NCommands::Parse(conf, mods, values, src)).first;
    return result->second;
}

TCommands::TInliner::TVarDefinition
TCommands::TInliner::GetVariableDefinition(NPolexpr::EVarId id) {
    auto name = Commands.Values.GetVarName(id);

    if (auto var = VarLookup(name))
        return {var, false};

    // legacy lookup

    if (
        Y_UNLIKELY(name.ends_with("__NO_UID__")) ||
        Y_UNLIKELY(name.ends_with("__NOINLINE__")) ||
        Y_UNLIKELY(name.ends_with("__LATEOUT__"))
    )
        return {};

    auto buildConf = GlobalConf();
    auto blockData = buildConf->BlockData.find(name);
    if (blockData != buildConf->BlockData.end())
        if (blockData->second.IsUserMacro || blockData->second.IsGenericMacro || blockData->second.IsFileGroupMacro)
            return {}; // TODO how do we _actually_ detect non-vars?

    auto var = LegacyVars.VarLookup(name, buildConf);
    if (!var)
        return {};

    // check recursion

    auto depth = LegacyVars.RecursionDepth.try_emplace(id, 0).first;
    for (size_t i = 0; i != depth->second; ++i) {
        var = var->BaseVal;
        if (!var || var->DontExpand || var->IsReservedName)
            ythrow TError() << "self-contradictory variable definition (" << name << ")";
    }

    // validate contents

    if (var->size() != 1)
        ythrow TNotImplemented() << "unexpected variable size " << var->size() << " (" << name << ")";
    auto& val = var->at(0);
    if (!val.HasPrefix)
        ythrow TError() << "unexpected variable format";

    // check the parser cache

    auto defs = LegacyVars.DefinitionCache.find(name);
    if (defs == LegacyVars.DefinitionCache.end())
        defs = LegacyVars.DefinitionCache.try_emplace(name, MakeHolder<TLegacyVars::TDefinitions>()).first;
    else
        if (depth->second < defs->second->size())
            return {(*defs->second)[depth->second].Get(), true}; // already processed
    Y_ASSERT(defs->second->size() == depth->second);

    // do the thing

    ui64 scopeId;
    TStringBuf cmdName;
    TStringBuf cmdValue;
    ParseLegacyCommandOrSubst(val.Name, scopeId, cmdName, cmdValue);
    defs->second->push_back(MakeHolder<NCommands::TSyntax>(Commands.Parse(Conf, Commands.Mods, Commands.Values, TString(cmdValue))));
    return {defs->second->back().Get(), true};
}

const NCommands::TSyntax*
TCommands::TInliner::GetMacroDefinition(NPolexpr::EVarId id) {
    auto name = Commands.Values.GetVarName(id);

    // TODO preliminary validation

    // legacy lookup

    auto buildConf = GlobalConf();
    auto var = LegacyVars.VarLookup(name, buildConf);

    // check recursion

    Y_ASSERT(!LegacyVars.RecursionDepth.contains(id)); // it's a variable-only feature

    // validate contents

    Y_ASSERT(var);
    Y_ASSERT(var->size() == 1);
    Y_ASSERT(var->front().HasPrefix);
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
        return defs->second->back().Get(); // already processed
    }
    Y_ASSERT(defs->second->empty());

    // do the thing

    auto defBody = MacroDefBody(subValue);
    defs->second->push_back(MakeHolder<NCommands::TSyntax>(Commands.Parse(Conf, Commands.Mods, Commands.Values, TString(defBody))));
    return defs->second->back().Get();
}

const NCommands::TSyntax* TCommands::TInliner::VarLookup(TStringBuf name) {
    for (auto scope = Scope; scope; scope = scope->Base) {
        auto it = scope->VarDefinitions.find(name);
        if (it != scope->VarDefinitions.end())
            return &it->second;
    }
    return {};
}

const TYVar* TCommands::TInliner::TLegacyVars::VarLookup(TStringBuf name, const TBuildConfiguration* conf) {
    // do not expand vars that have been overridden
    // (e.g., ignore the global `SRCFLAGS=` from `ymake.core.conf`
    // while dealing with the `SRCFLAGS` parameter in `_SRC()`)
    for (auto macroVars = &AllVars; macroVars; macroVars = macroVars->Base) {
        if (macroVars == &InlineVars)
            break;
        if (macroVars->Contains(name))
            return {};
    }

    auto var = InlineVars.Lookup(name);
    if (!var || var->DontExpand)
        return {};
    Y_ASSERT(!var->BaseVal || !var->IsReservedName); // TODO find out is this is so
    if (conf->CommandConf.IsReservedName(name))
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
        // TBD: TModifier::TValueTerm does not support TUnexpanded; do we need it there, as well?
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
        // TBD: TModifier::TValueTerm does not support TUnexpanded; do we need it there, as well?
        return std::move(x);
    }

private:
    NCommands::TSyntax& S;
    const bool ArgMode;

};

void TCommands::TInliner::FillMacroArgs(const NCommands::TSyntax::TCall& src, TScope& dst) {
    auto macroName = Commands.Values.GetVarName(src.Function);

    Y_ASSERT(Conf);
    auto blockDataIt = Conf->BlockData.find(macroName);
    const TBlockData* blockData = blockDataIt != Conf->BlockData.end() ? &blockDataIt->second : nullptr;
    Y_ASSERT(blockData); // TODO handle unknown macros

    Y_ASSERT(blockData->CmdProps->ArgNames.size() == src.Arguments.size());
    auto argCnt = src.Arguments.size();
    for (size_t i = 0; i != argCnt; ++i) {
        auto argName = TStringBuf(blockData->CmdProps->ArgNames[i]);
        if (argName.EndsWith(NStaticConf::ARRAY_SUFFIX))
            argName.Chop(strlen(NStaticConf::ARRAY_SUFFIX));
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

void TCommands::TInliner::InlineModValueTerm(
    const NCommands::TSyntax::TTransformation::TModifier::TValueTerm& term,
    NCommands::TSyntax::TTransformation::TModifier::TValue& writer
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
                if (def.Legacy && LegacyVars.RecursionDepth[id] != 0)
                    ythrow TError() << "unexpected multiargument substitution: " << Commands.Values.GetVarName(id);
                // leave it for the actual evaluation to expand
                writer.push_back(id);
                return;
            }
            auto recurse = [&](auto subId) {
                if (def.Legacy) {
                    Y_ASSERT(LegacyVars.RecursionDepth.contains(id));
                    ++LegacyVars.RecursionDepth[id];
                    Y_DEFER {--LegacyVars.RecursionDepth[id];};
                    InlineModValueTerm(subId, writer);
                } else
                    InlineModValueTerm(subId, writer);
            };
            for (auto&& thatTerm : def.Definition->Script[0][0]) {
                std::visit(TOverloaded{
                    [&](NPolexpr::TConstId subId) {
                        recurse(subId);
                    },
                    [&](NPolexpr::EVarId subId) {
                        recurse(subId);
                    },
                    [&](const NCommands::TSyntax::TTransformation& xfm) {
                        if (xfm.Mods.empty() && xfm.Body.size() == 1 && xfm.Body.front().size() == 1 && std::visit(TOverloaded{
                            [&](NPolexpr::EVarId subId) {
                                recurse(subId);
                                return true;
                            },
                            [&](const NCommands::TSyntax::TUnexpanded& x) {
                                writer.push_back(x.Variable);
                                return true;
                            },
                            [](auto&) {
                                return false;
                            }
                        }, xfm.Body.front().front()))
                            return;
                        ythrow TError() << "cannot sub-substitute";
                    },
                    [&](const NCommands::TSyntax::TCall&) {
                        throw TNotImplemented();
                    },
                    [&](const NCommands::TSyntax::TIdOrString&) {
                        Y_ABORT();
                    },
                    [&](const NCommands::TSyntax::TUnexpanded& x) {
                        writer.push_back(x.Variable);
                    },
                }, thatTerm);
            }
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
    for (auto& term : arg) {
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
                    if (!def.Legacy || LegacyVars.RecursionDepth[var] != 0)
                        ythrow TError() << "unexpected multiargument substitution: " << Commands.Values.GetVarName(var);
                    // TODO dev warning?
                    // leave it for the actual evaluation to expand
                    writer.WriteTerm(var);
                    return;
                }
                if (def.Legacy) {
                    Y_ASSERT(LegacyVars.RecursionDepth.contains(var));
                    ++LegacyVars.RecursionDepth[var];
                    Y_DEFER {--LegacyVars.RecursionDepth[var];};
                    InlineScalarTerms(def.Definition->Script[0][0], writer);
                } else
                    InlineScalarTerms(def.Definition->Script[0][0], writer);
            },
            [&](const NCommands::TSyntax::TTransformation& xfm) {
                auto newXfm = NCommands::TSyntax::TTransformation();
                newXfm.Mods.reserve(xfm.Mods.size());
                for (auto& mod : xfm.Mods) {
                    newXfm.Mods.push_back({mod.Name, {}});
                    auto& newMod = newXfm.Mods.back();
                    newMod.Values.reserve(mod.Values.size());
                    for (auto& val : mod.Values) {
                        auto& newVal = newMod.Values.emplace_back();
                        newVal.reserve(val.size());
                        for (auto& term : val) {
                            InlineModValueTerm(term, newVal);
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
                            if (def.Legacy) {
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
                            newXfm.Body = std::move(newBody.Script[0]);
                            return true;
                        }
                    }
                    return false;
                }();
                if (!bodyInlined)
                    newXfm.Body = xfm.Body;
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
            [&](const NCommands::TSyntax::TIdOrString&) {
                Y_ABORT();
            },
            [&](const NCommands::TSyntax::TUnexpanded& x) {
                writer.WriteTerm(x);
            },
        }, term);
    }
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
        if (def.Legacy) {
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
        if (auto var = std::get_if<NPolexpr::EVarId>(&term))
            if (doAVariable(*var))
                return true;
        if (auto xfm = std::get_if<NCommands::TSyntax::TTransformation>(&term))
            if (xfm->Mods.empty())
                if (xfm->Body.size() == 1 && xfm->Body.front().size() == 1)
                    if (auto var = std::get_if<NPolexpr::EVarId>(&xfm->Body.front().front()))
                        if (doAVariable(*var))
                            return true;
        if (auto call = std::get_if<NCommands::TSyntax::TCall>(&term))
            if (doACall(*call))
                return true;
        return false;
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
        if (def.Legacy) {
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
        if (auto var = std::get_if<NPolexpr::EVarId>(&term); var)
            if (doAVariable(*var))
                return true;
        if (auto xfm = std::get_if<NCommands::TSyntax::TTransformation>(&term))
            if (xfm->Mods.empty())
                if (xfm->Body.size() == 1 && xfm->Body.front().size() == 1)
                    if (auto var = std::get_if<NPolexpr::EVarId>(&xfm->Body.front().front()); var)
                        if (doAVariable(*var))
                            return true;
        if (auto call = std::get_if<NCommands::TSyntax::TCall>(&term))
            if (doACall(*call))
                return true;
        return false;
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
    const TBuildConfiguration* conf,
    const TVars& inlineVars,
    const TVars& allVars,
    bool preevaluate,
    EOutputAccountingMode oam
) {
    auto inliner = TInliner(conf, *this, inlineVars, allVars);
    auto& cachedAst = Parse(conf, Mods, Values, TString(cmd));
    auto ast = inliner.Inline(cachedAst);
    // TODO? VarRecursionDepth.clear(); // or clean up individual items as we go?
    if (preevaluate)
        return Preevaluate(ast, allVars, oam);
    else
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
                        os << mod.Name;
                        if (!mod.Values.empty())
                            os << "/" << mod.Values.size(); // TODO print values
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
    return std::visit(TOverloaded{
        [](std::string_view             val) { return fmt::format("'{}'", val); },
        [](TMacroValues::TTool          val) { return fmt::format("Tool{{'{}'}}", val.Data); },
        [](TMacroValues::TInput         val) { return fmt::format("Input{{{}}}", val.Coord); },
        [](TMacroValues::TInputs        val) { return fmt::format("Inputs{{{}}}", fmt::join(val.Coords, " ")); },
        [](TMacroValues::TOutput        val) { return fmt::format("Output{{{}}}", val.Coord); },
        [](TMacroValues::TGlobPattern   val) { return fmt::format("GlobPattern{{{}}}", val.Data); }
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
        if (!nodeStr.Empty())
            sink(nodeStr.data(), nodeStr.size());
    }
}

NCommands::TCompiledCommand TCommands::Preevaluate(NCommands::TSyntax& expr, const TVars& vars, EOutputAccountingMode oam) {
    NCommands::TCompiledCommand result;
    switch (oam) {
        case EOutputAccountingMode::Default:
            break;
        case EOutputAccountingMode::Module:
            // the zeroth entry is the main output, see TMakeCommand::MineInputsAndOutputs
            result.Outputs.Base = 1;
            break;
    }
    auto reducer = TRefReducer{Mods, Values, vars, result};
    reducer.ReduceIf(expr);
    return result;
}

void TCommands::WriteShellCmd(
    ICommandSequenceWriter* writer,
    const NPolexpr::TExpression& cmdExpr,
    const TVars& vars,
    const TVector<std::span<TVarStr>>& inputs,
    TCommandInfo& cmd,
    const TCmdConf* cmdConf,
    TErrorShowerState* errorShower
) const {
    NCommands::TScriptEvaluator se(this, cmdConf, &vars, &inputs, &cmd);
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
        [](TMacroValues::TGlobPattern val) { return TString(val.Data); }
    }, Values.GetValue(node));
}

TString TCommands::PrintRawCmdNode(NPolexpr::EVarId node) const {
    return TString(Values.GetVarName(node));
}
