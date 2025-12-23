#include <devtools/ymake/commands/compilation.h>
#include <devtools/ymake/commands/mod_registry.h>
#include <devtools/ymake/lang/cmd_parser.h>

#include <fmt/format.h>
#include <util/generic/overloaded.h>

namespace NCommands {

    class TPreevaluator {

    public:

        using TInputs = TCompiledCommand::TInputs;
        using TOutputs = TCompiledCommand::TOutputs;

        TPreevaluator(
            const TModRegistry& mods,
            TMacroValues& values,
            const TVars& vars
        )
            : Mods(mods)
            , Values(values)
            , Vars(vars)
        {
        }

    public:

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
                args.push_back(Evaluate(Mods.Func2Id(EMacroFunction::Terms), terms));
            }
            return Evaluate(Mods.Func2Id(EMacroFunction::Args), args);
        }

        TMacroValues::TValue EvalTerm(const TSyntax::TTerm& term) {
            return std::visit(TOverloaded{
                [&](TMacroValues::TValue val) -> TMacroValues::TValue {
                    return val;
                },
                [&](NPolexpr::EVarId id) -> TMacroValues::TValue {
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
                args.push_back(Evaluate(Mods.Func2Id(EMacroFunction::Cat), catArgs));
            }
            args.push_back(val);
            return Evaluate(Mods.Func2Id(mod.Function), args);
        }

    private:

        TMacroValues::TValue Evaluate(NPolexpr::TConstId id) {
            return Values.GetValue(id);
        }

        TMacroValues::TValue Evaluate(NPolexpr::EVarId id) {
            // TBD what about vector vars here?
            auto name = Values.GetVarName(id);
            auto var = Vars.Lookup(name);
            if (!var || var->DontExpand /* see InitModuleVars() */) {
                // this is a Very Special Case that is supposed to handle things like `${input:FOOBAR}` / `FOOBAR=$ARCADIA_ROOT/foobar`;
                // note that the extra braces in the result are significant:
                // the pattern should match whatever `TPathResolver::ResolveAsKnown` may expect to see
                return TMacroValues::TXString{fmt::format("${{{}}}", name)};
            }
            return TMacroValues::TXString{std::string(Eval1(var))};
        }

        TMacroValues::TValue Evaluate(NPolexpr::TFuncId id, std::span<TMacroValues::TValue> args) {
            TCompiledCommand sink;
            auto fnIdx = static_cast<EMacroFunction>(id.GetIdx());
            auto mod = Mods.At(fnIdx);
            if (Y_LIKELY(mod && mod->CanPreevaluate)) {
                return mod->Preevaluate({Values, sink}, args);
                // TODO verify that the sink did not catch any side effects
            }
            Y_DEBUG_ABORT_UNLESS(mod);
            throw TConfigurationError()
                << "Cannot preevaluate modifier [[bad]]" << ToString(fnIdx) << "[[rst]]";
        }

    private:

        const TModRegistry& Mods;
        TMacroValues& Values;
        const TVars& Vars;

    };

}
