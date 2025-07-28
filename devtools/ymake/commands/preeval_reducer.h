#include <devtools/ymake/commands/compilation.h>
#include <devtools/ymake/commands/mod_registry.h>
#include <devtools/ymake/lang/cmd_parser.h>

#include <fmt/format.h>
#include <util/generic/overloaded.h>

namespace NCommands {

    struct TRefReducer {

        using TInputs = TCompiledCommand::TInputs;
        using TOutputs = TCompiledCommand::TOutputs;

        TRefReducer(
            const TModRegistry& mods,
            TMacroValues& values,
            const TVars& vars,
            TCompiledCommand& sink
        )
            : Mods(mods)
            , Values(values)
            , Vars(vars)
            , Sink(sink)
        {
        }

    public:

        void ReduceIf(TSyntax& ast) {
            for (auto& cmd : ast.Script)
                ReduceCmd(cmd);
            Sink.Expression = Compile(Mods, ast, Values);
        }

    private:

        void ReduceCmd(TSyntax::TCommand& cmd) {
            for (auto& arg : cmd)
                for (auto& term : arg)
                    if (auto xfm = std::get_if<TSyntax::TTransformation>(&term)) {
                        auto cutoff = std::find_if(
                            xfm->Mods.begin(), xfm->Mods.end(),
                            [&](auto& mod) {return Condition(Mods.Func2Id(mod.Function));}
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

        TMacroValues::TValue EvalCmd(const TSyntax::TCommand& cmd) {
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
            TVector<TMacroValues::TValue> args;
            args.reserve(cmd.size());
            for (auto& arg : cmd) {
                TVector<TMacroValues::TValue> terms;
                terms.reserve(arg.size());
                for (auto& term : arg)
                    terms.push_back(EvalTerm(term));
                args.push_back(Evaluate(Mods.Func2Id(EMacroFunction::Terms), std::span(terms)));
            }
            return Evaluate(Mods.Func2Id(EMacroFunction::Args), std::span(args));
        }

        TMacroValues::TValue EvalTerm(const TSyntax::TTerm& term) {
            return std::visit(TOverloaded{
                [&](TMacroValues::TValue val) {
                    return val;
                },
                [&](NPolexpr::EVarId id) {
                    return Evaluate(id);
                },
                [&](const TSyntax::TTransformation& xfm) -> TMacroValues::TValue {
                    auto val = EvalCmd(xfm.Body);
                    for (auto& mod : std::ranges::reverse_view(xfm.Mods))
                        val = ApplyMod(mod, val);
                    return val;
                },
                [&](const TSyntax::TCall&) -> TMacroValues::TValue {
                    Y_ABORT();
                },
                [&](const TSyntax::TBuiltinIf&) -> TMacroValues::TValue {
                    Y_ABORT();
                },
                [&](const TSyntax::TIdOrString&) -> TMacroValues::TValue {
                    Y_ABORT();
                },
                [&](const TSyntax::TUnexpanded&) -> TMacroValues::TValue {
                    Y_ABORT();
                }
            }, term);
        }

        TMacroValues::TValue ApplyMod(const TSyntax::TTransformation::TModifier& mod, TMacroValues::TValue val) {
            TVector<TMacroValues::TValue> args;
            args.reserve(mod.Arguments.size() + 1);
            for (auto& modArg : mod.Arguments) {
                TVector<TMacroValues::TValue> catArgs;
                catArgs.reserve(modArg.size());
                for (auto& modTerm : modArg)
                    catArgs.push_back(EvalTerm(modTerm));
                args.push_back(Evaluate(Mods.Func2Id(EMacroFunction::Cat), std::span(catArgs)));
            }
            args.push_back(val);
            return Evaluate(Mods.Func2Id(mod.Function), std::span(args));
        }

    private:

        bool Condition(NPolexpr::TFuncId func) {
            RootFnIdx = static_cast<EMacroFunction>(func.GetIdx());
            auto mod = Mods.At(RootFnIdx);
            if (Y_LIKELY(mod))
                return mod->MustPreevaluate;
            Y_DEBUG_ABORT();
            return false;
        };

        std::string Evaluate(const TVarStr& val) {
            if (val.HasPrefix)
                return std::string(GetCmdValue(val.Name));
            else
                return val.Name;
        }

        TMacroValues::TValue Evaluate(NPolexpr::EVarId id) {
            auto name = Values.GetVarName(id);
            auto var = Vars.Lookup(name);
            if (!var) {
                // this is a special case for when we get a macro argument like IN = "$L/TEXT/$U/__init__.py":
                // the "$L" etc. are not variables, but root markers (see `NPath::ERoot` & Co.), we should keep them;
                // TODO: _ackshually_, we should mark/type the whole thing as a file path and never parse it to begin with
                return TMacroValues::TXString{fmt::format("${}", name)};
            }
            if (var->DontExpand /* see InitModuleVars() */) {
                // this is a Very Special Case that is supposed to handle things like `${input:FOOBAR}` / `FOOBAR=$ARCADIA_ROOT/foobar`;
                // note that the extra braces in the result are significant:
                // the pattern should match whatever `TPathResolver::ResolveAsKnown` may expect to see
                return TMacroValues::TXString{fmt::format("${{{}}}", name)};
            }
            // TODO? support for TVarStr with .StructCmdForVars
            if (var->size() == 1) {
                auto& val = var->front();
                return TMacroValues::TXString{Evaluate(val)};
            } else {
                TMacroValues::TXStrings result;
                result.Data.reserve(var->size());
                for (const auto &val : *var)
                    result.Data.push_back(Evaluate(val));
                return result;
            }
        }

        TMacroValues::TValue Evaluate(NPolexpr::TFuncId id, std::span<TMacroValues::TValue> args) {
            auto fnIdx = static_cast<EMacroFunction>(id.GetIdx());
            auto mod = Mods.At(fnIdx);
            if (Y_LIKELY(mod && mod->CanPreevaluate)) {
                return mod->Preevaluate({Values, Sink}, args);
            }
            Y_DEBUG_ABORT_UNLESS(mod);
            throw TConfigurationError()
                << "Cannot process modifier [[bad]]" << ToString(fnIdx) << "[[rst]]"
                << " while preevaluating [[bad]]" << ToString(RootFnIdx) << "[[rst]]";
        }

    private:

        const TModRegistry& Mods;
        TMacroValues& Values;
        const TVars& Vars;
        TCompiledCommand& Sink;
        EMacroFunction RootFnIdx;

    };

}
