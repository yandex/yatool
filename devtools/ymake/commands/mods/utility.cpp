#include "common.h"

#include <devtools/ymake/config/config.h> // for GetOrInit()

#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    inline bool IsEmpty(const TModMetadata& mod, const TMacroValues::TValue& val) {
        return std::visit(TOverloaded{
            [](std::monostate) {
                return true;
            },
            [&](std::string_view x) {
                return x.empty();
            },
            [&](const std::vector<std::string_view>& x) {
                return x.empty();
            },
            [&](auto&& x) -> bool {
                throw TBadArgType(mod.Name, x);
            },
        }, val);
    }

    inline bool IsEmpty(const TTermValue& val) {
        return std::visit(TOverloaded{
            [](TTermError) -> bool {
                Y_ABORT();
            },
            [](TTermNothing) {
                return true;
            },
            [&](const TString& x) {
                return x.empty();
            },
            [&](const TVector<TString>& x) {
                return x.empty();
            },
            [&](const TTaggedStrings& x) {
                return x.empty();
            }
        }, val);
    }

    inline bool AsBool(const TModMetadata& mod, const TMacroValues::TValue& val) {
        return std::visit(TOverloaded{
            [](bool x) {
                return x;
            },
            [&](auto&& x) -> bool {
                throw TBadArgType(mod.Name, x);
            },
        }, val);
    }

    inline bool ToBool(const TTermValue& val) {
        return std::visit(TOverloaded{
            [](TTermError) -> bool {
                Y_ABORT();
            },
            [](TTermNothing) {
                return false;
            },
            [&](auto&&) {
                return true;
            }
        }, val);
    }

    inline TTermValue FromBool(bool val) {
        return val ? TTermValue(TString()) : TTermValue(TTermNothing());
    }

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
        THideEmpty(): TBasicModImpl({.Id = EMacroFunction::HideEmpty, .Name = "hideempty", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            if (IsEmpty(*this, args[0]))
                return std::monostate();
            return args[0];
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            if (IsEmpty(args[0]))
                return TTermNothing();
            return args[0];
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TEmpty: public TBasicModImpl {
    public:
        TEmpty(): TBasicModImpl({.Id = EMacroFunction::Empty, .Name = "empty", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return IsEmpty(*this, args[0]);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return FromBool(IsEmpty(args[0]));
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
            return FromBool(!IsEmpty(args[0]));
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TNot: public TBasicModImpl {
    public:
        TNot(): TBasicModImpl({.Id = EMacroFunction::Not, .Name = "not", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return !AsBool(*this, args[0]);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return FromBool(!ToBool(args[0]));
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TParseBool: public TBasicModImpl {
    public:
        TParseBool(): TBasicModImpl({.Id = EMacroFunction::ParseBool, .Name = "parse_bool", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](std::string_view arg) -> TMacroValues::TValue {
                    return NYMake::IsTrue(arg);
                },
                [&](const std::vector<std::string_view>& arg) -> TMacroValues::TValue {
                    if (arg.size() != 1)
                        throw TBadArgType(Name, arg);
                    return NYMake::IsTrue(arg.front());
                },
                [&](auto&& x) -> TMacroValues::TValue {
                    throw TBadArgType(Name, x);
                },
            }, args[0]);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [&](const TString& arg) -> TTermValue {
                    return FromBool(NYMake::IsTrue(arg));
                },
                [&](const TVector<TString>& arg) -> TTermValue {
                    if (arg.size() != 1)
                        throw TBadArgType(Name, arg);
                    return FromBool(NYMake::IsTrue(arg.front()));
                },
                [&](auto&& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
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
                    for (const auto& s: v) {
                        writer->WriteEnv(ctx.CmdInfo.SubstMacroDeeply(nullptr, s, ctx.Vars, false));
                    }
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
                    if (v.empty()) {
                        return TTermNothing();
                    } else if (v.size() == 1) {
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
