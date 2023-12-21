#pragma once

#include "evaluation_common.h"

#include <devtools/ymake/command_store.h>
#include <devtools/ymake/polexpr/expression.h>

namespace NCommands {

    struct TArgAccumulator;

    class TScriptEvaluator {

    public:

        const TCommands* Commands;
        const TCmdConf* CmdConf;
        const TVars* Vars;
        TCommandInfo* CmdInfo;

    public:

        size_t DoScript(const NPolexpr::TExpression* expr, size_t scrBegin, ICommandSequenceWriter* writer);

    private:

        size_t DoTermAsScript(const NPolexpr::TExpression* /*expr*/, size_t begin, ICommandSequenceWriter* writer);
        size_t DoCommand(const NPolexpr::TExpression* expr, size_t cmdBegin, ICommandSequenceWriter* writer);
        size_t DoTermAsCommand(const NPolexpr::TExpression* expr, size_t termBegin, ICommandSequenceWriter* writer);
        size_t DoArgument(const NPolexpr::TExpression* expr, size_t argBegin, ICommandSequenceWriter* writer);
        size_t DoTerm(const NPolexpr::TExpression* expr, size_t begin, TArgAccumulator* writer);

    private:

        const NPolexpr::TExpression* AsSubexpression(const TStringBuf& val);
        const NPolexpr::TExpression* AsSubexpression(const TYVar* var);

        TTermValue EvalFn(
            NPolexpr::TFuncId id, std::span<const TTermValue> args,
            const TEvalCtx& ctx, const NPolexpr::TExpression* expr,
            ICommandSequenceWriter* writer
        );

        void AppendTerm(TTermValue&& term, TArgAccumulator* writer);

        auto GetFnArgs(const NPolexpr::TExpression& expr, size_t pos, EMacroFunctions expected);

    };

}
