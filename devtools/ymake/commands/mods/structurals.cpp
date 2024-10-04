#include "common.h"

#include <util/generic/overloaded.h>
#include <util/string/escape.h>

using namespace NCommands;

namespace {

    class TCmds: public NCommands::TBasicModImpl {
    public:
        TCmds(): TBasicModImpl({.Id = EMacroFunction::Cmds, .Name = "cmds", .Arity = 0, .Internal = true}) {
        }
    } Cmds;

    class TArgs: public NCommands::TBasicModImpl {
    public:
        TArgs(): TBasicModImpl({.Id = EMacroFunction::Args, .Name = "args", .Arity = 0, .Internal = true, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& unwrappedArgs
        ) const override {
            CheckArgCount(unwrappedArgs);
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
            return ctx.Values.GetValue(ctx.Values.InsertStr(result));
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            TVector<TString> result;
            for (auto& arg : args)
                result.push_back(std::visit(TOverloaded{
                    [](TTermError) -> TString {
                        Y_ABORT();
                    },
                    [&](TTermNothing x) -> TString {
                        throw TBadArgType(Name, x);
                    },
                    [&](const TString& s) -> TString {
                        return s;
                    },
                    [&](const TVector<TString>& x) -> TString {
                        throw TBadArgType(Name, x);
                    },
                    [&](const TTaggedStrings& x) -> TString {
                        throw TBadArgType(Name, x);
                    }
                }, arg));
            return result;
        }
    } Args;

}
