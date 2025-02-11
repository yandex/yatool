#include "common.h"

#include <devtools/ymake/command_helpers.h>
#include <util/generic/overloaded.h>
#include <util/string/escape.h>

using namespace NCommands;

namespace {

    class TCmds: public TBasicModImpl {
    public:
        TCmds(): TBasicModImpl({.Id = EMacroFunction::Cmds, .Name = "cmds", .Arity = 0, .Internal = true}) {
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TArgs: public TBasicModImpl {
    public:
        TArgs(): TBasicModImpl({.Id = EMacroFunction::Args, .Name = "args", .Arity = 0, .Internal = true, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            if (Y_UNLIKELY(args.size() == 1 && std::holds_alternative<TMacroValues::TLegacyLateGlobPatterns>(args.front()))) {
                return args.front();
            }
            std::vector<std::string_view> result;
            result.reserve(args.size());
            for (auto& arg : args)
                result.push_back(std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(std::get<std::string_view>(arg)))));
            return result;
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
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TTerms: public TBasicModImpl {
    public:
        TTerms(): TBasicModImpl({.Id = EMacroFunction::Terms, .Name = "terms", .Arity = 0, .Internal = true, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TTerms(TModMetadata metadata): TBasicModImpl(metadata) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto result = TString();
            if (Y_UNLIKELY(args.size() == 1 && std::holds_alternative<TMacroValues::TLegacyLateGlobPatterns>(args.front()))) {
                return args.front();
            }
            for (auto& arg : args) {
                if (auto piece = std::get_if<std::string_view>(&arg)) {
                    result += *piece;
                } else if (auto pieces = std::get_if<std::vector<std::string_view>>(&arg)) {
                    for (auto& piece : *pieces)
                        result += piece;
                }
            }
            return ctx.Values.GetValue(ctx.Values.InsertStr(result));
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            TString result;
            for (auto& arg : args)
                result += std::visit(TOverloaded{
                    [](TTermError) -> TString {
                        Y_ABORT();
                    },
                    [&](TTermNothing x) -> TString {
                        throw TBadArgType(Name, x);
                    },
                    [&](const TString& s) -> TString {
                        return s;
                    },
                    [&](const TVector<TString>& v) -> TString {
                        if (v.empty())
                            return {};
                        else if (v.size() == 1)
                            return v.front();
                        else
                            throw TNotImplemented() << "Nested terms should not have multiple items";
                    },
                    [&](const TTaggedStrings& x) -> TString {
                        throw TBadArgType(Name, x);
                    }
                }, arg);
            return result;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TCat: public TTerms {
        // this is basically `terms` as a proper function rather than a manifestation of a syntactical element (`TSyntax::TTerm` sequence)
    public:
        TCat(): TTerms({.Id = EMacroFunction::Cat, .Name = "cat", .Arity = 0, .Internal = true, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
