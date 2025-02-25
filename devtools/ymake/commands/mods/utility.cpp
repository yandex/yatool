#include "common.h"

#include <devtools/ymake/config/config.h> // for GetOrInit()

#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    //
    //
    //

    class THide: public TBasicModImpl {
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
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class THideEmpty: public TBasicModImpl {
    public:
        THideEmpty(): TBasicModImpl({.Id = EMacroFunction::HideEmpty, .Name = "hideempty", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing) -> TTermValue {
                    return TTermNothing();
                },
                [&](const TString& x) -> TTermValue {
                    if (x.empty())
                        return TTermNothing();
                    return x;
                },
                [&](const TVector<TString>& x) -> TTermValue {
                    if (x.empty())
                        return TTermNothing();
                    return x;
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TClear: public TBasicModImpl {
    public:
        TClear(): TBasicModImpl({.Id = EMacroFunction::Clear, .Name = "clear", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing) -> TTermValue {
                    return TTermNothing();
                },
                [&](const TString&) -> TTermValue {
                    return TString();
                },
                [&](const TVector<TString>& v) -> TTermValue {
                    if (v.empty())
                        return TTermNothing();
                    return TString();
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TCwd: public TBasicModImpl {
    public:
        TCwd(): TBasicModImpl({.Id = EMacroFunction::Cwd, .Name = "cwd", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) {
                    writer->WriteCwd(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                },
                [&](const TVector<TString>& v) {
                    if (v.empty())
                        return;
                    else if (v.size() == 1)
                        writer->WriteCwd(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
                    else
                        throw TNotImplemented() << "Cwd does not support arrays";
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TStdout: public TBasicModImpl {
    public:
        TStdout(): TBasicModImpl({.Id = EMacroFunction::AsStdout, .Name = "stdout", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) {
                    writer->WriteStdout(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                },
                [&](const TVector<TString>& v) {
                    if (v.empty())
                        return;
                    else if (v.size() == 1)
                        writer->WriteStdout(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
                    else
                        throw TNotImplemented() << "StdOut does not support arrays";
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TEnv: public TBasicModImpl {
    public:
        TEnv(): TBasicModImpl({.Id = EMacroFunction::SetEnv, .Name = "env", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) {
                    writer->WriteEnv(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                },
                [&](const TVector<TString>& v) {
                    if (v.empty())
                        return;
                    else if (v.size() == 1)
                        writer->WriteEnv(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
                    else
                        throw TNotImplemented() << "Env does not support arrays";
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TResource: public TBasicModImpl {
    public:
        TResource(): TBasicModImpl({.Id = EMacroFunction::ResourceUri, .Name = "resource", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) {
                    writer->WriteResource(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                },
                [&](const TVector<TString>& v) {
                    if (v.empty())
                        return;
                    else if (v.size() == 1)
                        writer->WriteResource(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
                    else
                        throw TNotImplemented() << "Resource does not support arrays";
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TTared: public TBasicModImpl {
    public:
        TTared(): TBasicModImpl({.Id = EMacroFunction::Tared, .Name = "tared", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) -> TTermValue {
                    writer->WriteTaredOut(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                    return s;
                },
                [&](const TVector<TString>& v) -> TTermValue {
                    if (v.size() == 1) {
                        writer->WriteTaredOut(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
                        return v.front();
                    } else
                        throw TNotImplemented() << "Tared outputs do not support arrays";
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TRequirements: public TBasicModImpl {
    public:
        TRequirements(): TBasicModImpl({.Id = EMacroFunction::Requirements, .Name = "requirements", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& s) {
                    ctx.CmdInfo.WriteRequirements(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                },
                [&](const TVector<TString>& v) {
                    if (v.empty())
                        return;
                    else if (v.size() == 1)
                        ctx.CmdInfo.WriteRequirements(ctx.CmdInfo.SubstMacroDeeply(nullptr, v.front(), ctx.Vars, false));
                    else
                        throw TNotImplemented() << "Requirements do not support arrays";
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TKeyValue: public TBasicModImpl {
    public:
        TKeyValue(): TBasicModImpl({.Id = EMacroFunction::KeyValue, .Name = "kv", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            std::visit(TOverloaded{
                [](TTermError) {
                    Y_ABORT();
                },
                [&](TTermNothing x) {
                    throw TBadArgType(Name, x);
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
                [&](const TVector<TString>& v) {
                    if (v.size() == 2)
                        GetOrInit(ctx.CmdInfo.KV)[v[0]] = v[1];
                    else if (v.size() == 1)
                        GetOrInit(ctx.CmdInfo.KV)[v[0]] = "yes";
                    else
                        throw TNotImplemented() << "bad KV item count";
                },
                [&](const TTaggedStrings& x) {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
            return TTermNothing();
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

}
