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
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            auto prefix = std::move(std::get<TMacroValues::TXString>(args[0]));
            return std::visit(TOverloaded{
                [&](TMacroValues::TXString&& body) -> TMacroValues::TValue {
                    auto result = std::move(prefix);
                    result.Data += body.Data;
                    return result;
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    auto result = TMacroValues::TXStrings();
                    result.Data.reserve(bodies.Data.size());
                    for (auto& body : bodies.Data) {
                        auto item = std::string();
                        item.reserve(prefix.Data.size() + body.size());
                        item = prefix.Data;
                        item += body;
                        result.Data.push_back(std::move(item));
                    }
                    return result;
                },
                [&](const auto& x) -> TMacroValues::TValue {
                    throw TBadArgType(Name, x);
                }
            }, std::move(args[1]));
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
                    return NToOne(args[0], body);
                },
                [&](const TVector<TString>& bodies) -> TTermValue {
                    if (bodies.size() == 1)
                        return NToOne(args[0], bodies.front());
                    return NToMany(args[0], bodies);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }

            }, args[1]);
        }
    private:
        TTermValue NToOne(const TTermValue& prefixArg, const TString& body) const {
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& prefix) -> TTermValue {
                    return OneToOne(prefix, body);
                },
                [&](const TVector<TString>& prefixes) -> TTermValue {
                    return ManyToOne(prefixes, body);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, prefixArg);
        }
        TTermValue NToMany(const TTermValue& prefixArg, const TVector<TString>& bodies) const {
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [&](TTermNothing x) -> TTermValue {
                    throw TBadArgType(Name, x);
                },
                [&](const TString& prefix) -> TTermValue {
                    return OneToMany(prefix, bodies);
                },
                [&](const TVector<TString>& prefixes) -> TTermValue {
                    Y_UNUSED(prefixes);
                    ythrow TNotImplemented() << "Pre arguments should not both be arrays";
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, prefixArg);
        }
        TTermValue OneToOne(const TString& prefix, const TString& body) const {
            if (prefix.EndsWith(' ')) {
                auto trimmedPrefix = prefix.substr(0, 1 + prefix.find_last_not_of(' '));
                return TVector<TString>{std::move(trimmedPrefix), body};
            }
            return prefix + body;
        }
        TTermValue OneToMany(const TString& prefix, const TVector<TString>& bodies) const {
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
        }
        TTermValue ManyToOne(const TVector<TString>& prefixes, const TString& body) const {
            TVector<TString> result;
            result.reserve(prefixes.size());
            for (auto& prefix : prefixes)
                result.push_back(prefix + body);
            return std::move(result);
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
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            auto suf = std::get<TMacroValues::TXString>(args[0]);
            return std::visit(TOverloaded{
                [&](const TMacroValues::TXString& body) -> TMacroValues::TValue {
                    return TMacroValues::TXString{TString::Join(body.Data, suf.Data)};
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    auto result = TMacroValues::TXStrings();
                    result.Data.reserve(bodies.Data.size());
                    for (auto& body : bodies.Data)
                        result.Data.push_back(TString::Join(body, suf.Data));
                    return result;
                },
                [&](const auto& x) -> TMacroValues::TValue {
                    throw TBadArgType(Name, x);
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
        TJoin(): TBasicModImpl({.Id = EMacroFunction::Join, .Name = "join", .Arity = 2, .CanPreevaluate = true, .CanEvaluate = true}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            auto glue = std::get<TMacroValues::TXString>(args[0]);
            return std::visit(TOverloaded{
                [](std::monostate) -> TMacroValues::TValue {
                    return std::monostate();
                },
                [&](TMacroValues::TXString body) -> TMacroValues::TValue {
                    return body;
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    if (bodies.Data.empty())
                        return std::monostate();
                    return TMacroValues::TXString{JoinSeq(TString(glue.Data), bodies.Data)};
                },
                [&](const auto& x) -> TMacroValues::TValue {
                    throw TBadArgType(Name, x);
                }
            }, args[1]);
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
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            return std::visit(TOverloaded{
                [](std::monostate) -> TMacroValues::TValue {
                    return std::monostate();
                },
                [&](const TMacroValues::TXString& body) -> TMacroValues::TValue {
                    return TMacroValues::TXString{DoString(body.Data)};
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    return TMacroValues::TXString{DoStrings(bodies.Data)};
                },
                [&](const auto& x) -> TMacroValues::TValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
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
                    return DoString(body);
                },
                [&](const TVector<TString>& bodies) -> TTermValue {
                    return DoStrings(bodies);
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

        static TString DoString(const auto& s) {
            auto md5 = Md5Beg(s);
            return Md5End(md5);
        }

        static TString DoStrings(const auto& ss) {
            static const TStringBuf separator = "|";
            // Init and complete by separator for make different digests of empty string and empty array
            auto md5 = Md5Beg(separator);
            for (const auto& s: ss) {
                md5.Update(s);
                md5.Update(separator);
            }
            return Md5End(md5);
        }

    } Y_GENERATE_UNIQUE_ID(Mod);

}
