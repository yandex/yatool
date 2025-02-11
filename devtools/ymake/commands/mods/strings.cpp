#include "common.h"

#include <devtools/ymake/command_helpers.h>

#include <library/cpp/digest/md5/md5.h>
#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    //
    //
    //

    class TPre: public TBasicModImpl {
    public:
        TPre(): TBasicModImpl({.Id = EMacroFunction::Pre, .Name = "pre", .Arity = 2, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto prefix = std::get<std::string_view>(args[0]);
            return std::visit(TOverloaded{
                [&](std::string_view body) -> TMacroValues::TValue {
                    return ctx.Values.GetValue(ctx.Values.InsertStr(TString::Join(prefix, body)));
                },
                [&](const std::vector<std::string_view>& bodies) -> TMacroValues::TValue {
                    auto result = std::vector<std::string_view>();
                    result.reserve(bodies.size());
                    for (auto& body : bodies)
                        result.push_back(std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(TString::Join(prefix, body)))));
                    return result;
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[1]);
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

                [&](const TString& body) {
                    return std::visit(TOverloaded{
                        [](TTermError) -> TTermValue {
                            Y_ABORT();
                        },
                        [&](TTermNothing x) -> TTermValue {
                            throw TBadArgType(Name, x);
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
                        },
                        [&](const TTaggedStrings& x) -> TTermValue {
                            throw TBadArgType(Name, x);
                        }
                    }, args[0]);
                },

                [&](const TVector<TString>& bodies) -> TTermValue {
                    return std::visit(TOverloaded{
                        [](TTermError) -> TTermValue {
                            Y_ABORT();
                        },
                        [&](TTermNothing x) -> TTermValue {
                            throw TBadArgType(Name, x);
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
                        },
                        [&](const TTaggedStrings& x) -> TTermValue {
                            throw TBadArgType(Name, x);
                        }
                    }, args[0]);
                },

                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }

            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TSuf: public TBasicModImpl {
    public:
        TSuf(): TBasicModImpl({.Id = EMacroFunction::Suf, .Name = "suf", .Arity = 2, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto suf = std::get<std::string_view>(args[0]);
            return std::visit(TOverloaded{
                [&](std::string_view body) -> TMacroValues::TValue {
                    return ctx.Values.GetValue(ctx.Values.InsertStr(TString::Join(body, suf)));
                },
                [&](const std::vector<std::string_view>& bodies) -> TMacroValues::TValue {
                    auto result = std::vector<std::string_view>();
                    result.reserve(bodies.size());
                    for (auto& body : bodies)
                        result.push_back(std::get<std::string_view>(ctx.Values.GetValue(ctx.Values.InsertStr(TString::Join(body, suf)))));
                    return result;
                },
                [](auto&) -> TMacroValues::TValue {
                    throw std::bad_variant_access();
                }
            }, args[1]);
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto suffix = std::get<TString>(args[0]);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing) -> TTermValue {
                    return TTermNothing();
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
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TJoin: public TBasicModImpl {
    public:
        TJoin(): TBasicModImpl({.Id = EMacroFunction::Join, .Name = "join", .Arity = 2, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto glue = std::get<TString>(args[0]);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing) -> TTermValue {
                    return TTermNothing();
                },
                [&](const TString& body) -> TTermValue {
                    return body;
                },
                [&](const TVector<TString>& bodies) -> TTermValue {
                    if (bodies.empty())
                        return TTermNothing();
                    return JoinSeq(glue, bodies);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TToUpper: public TBasicModImpl {
    public:
        TToUpper(): TBasicModImpl({.Id = EMacroFunction::ToUpper, .Name = "toupper", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto apply = [](TString s) {
                s.to_upper();
                return s;
            };
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return apply(std::move(s));
                },
                [&](TVector<TString> v) -> TTermValue {
                    for (auto& s : v)
                        s = apply(std::move(s));
                    return std::move(v);
                },
                [&](TTaggedStrings x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TToLower: public TBasicModImpl {
    public:
        TToLower(): TBasicModImpl({.Id = EMacroFunction::ToLower, .Name = "tolower", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            auto apply = [](TString s) {
                s.to_lower();
                return s;
            };
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](TString s) -> TTermValue {
                    return apply(std::move(s));
                },
                [&](TVector<TString> v) -> TTermValue {
                    for (auto& s : v)
                        s = apply(std::move(s));
                    return std::move(v);
                },
                [&](TTaggedStrings x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TQuo: public TBasicModImpl {
    public:
        TQuo(): TBasicModImpl({.Id = EMacroFunction::Quo, .Name = "quo", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            // "quo" is used to wrap pieces of the unparsed command line;
            // the quotes in question should disappear after argument extraction,
            // so for the arg-centric model this modifier is effectively a no-op
            return args[0];
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class TQuoteEach: public TBasicModImpl {
    public:
        TQuoteEach(): TBasicModImpl({.Id = EMacroFunction::QuoteEach, .Name = "qe", .Arity = 1, .CanEvaluate = true}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return args[0]; // NOP for the same reason as "quo"
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    //
    //

    class THash: public TBasicModImpl {
    public:
        THash(): TBasicModImpl({.Id = EMacroFunction::Hash, .Name = "hash", .Arity = 1, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] const TVector<TMacroValues::TValue>& args
        ) const override {
            CheckArgCount(args);
            auto arg0 = std::get<std::string_view>(args[0]);
            auto md5 = Md5Beg(arg0);
            auto id = ctx.Values.InsertStr(Md5End(md5));
            return ctx.Values.GetValue(id);
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
                [&](const TString& body) -> TTermValue {
                    auto md5 = Md5Beg(body);
                    return TString{Md5End(md5)};
                },
                [&](const TVector<TString>& bodies) -> TTermValue {
                    static const TStringBuf separator = "|";
                    // Init and complete by separator for make different digests of empty string and empty array
                    auto md5 = Md5Beg(separator);
                    for (const auto& body: bodies) {
                        md5.Update(body);
                        md5.Update(separator);
                    }
                    return TString{Md5End(md5)};
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }

    private:
        static MD5 Md5Beg(const TStringBuf& arg) {
            MD5 md5;
            md5.Update(arg);
            return md5;
        }

        static std::string Md5End(MD5& md5) {
            char buffer[33]; // MD5 class require 33 bytes buffer
            md5.End(buffer);
            return std::string{buffer};
        }

    } Y_GENERATE_UNIQUE_ID(Mod);

}
