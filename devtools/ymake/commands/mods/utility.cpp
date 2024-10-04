#include "common.h"

using namespace NCommands;

namespace {

    class THide: public NCommands::TBasicModImpl {
    public:
        THide(): TBasicModImpl({.Id = EMacroFunction::Hide, .Name = "hide", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return TTermNothing();
        }
    } Hide;

    class TKeyValue: public NCommands::TBasicModImpl {
    public:
        TKeyValue(): TBasicModImpl({.Id = EMacroFunction::KeyValue, .Name = "kv", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            RenderKeyValue(ctx, args);
            return TTermNothing();
        }
    } KeyValue;

}
