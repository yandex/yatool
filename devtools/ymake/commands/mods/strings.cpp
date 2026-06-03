#include "common.h"

#include <devtools/ymake/command_helpers.h>

#include <library/cpp/digest/md5/md5.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/overloaded.h>
#include <util/string/cast.h>
#include <util/string/split.h>

using namespace NCommands;

namespace {

    //
    // Shared helpers for modifier implementations.
    //
    // The intent is to remove the boilerplate std::visit/TOverloaded blocks that
    // every modifier used to repeat.  The helpers come in two flavors:
    //   * "Map*"     — element-wise string -> string transformations.
    //   * "Expand*"  — string -> vector<string> transformations (e.g. split).
    // Each flavor has a "Pre" variant working on TMacroValues::TValue and an
    // "Eval" variant working on TTermValue.
    //
    // NOTE: the Pre- and Eval-pairs are intentionally near-zero-shared duplicates
    // because TMacroValues::TValue and TTermValue use different alternative types
    // (std::string vs TString; std::vector<std::string> vs TVector<TString>);
    // unifying them via templates makes the call sites significantly less readable
    // for very little gain.  Keep the two siblings in sync when editing.
    //

    template <class F>
    TMacroValues::TValue MapPreString(TStringBuf name, const TMacroValues::TValue& v, F&& fn) {
        // fn: std::string -> std::string
        return std::visit(TOverloaded{
            [](std::monostate) -> TMacroValues::TValue {
                return std::monostate();
            },
            [&](const TMacroValues::TXString& s) -> TMacroValues::TValue {
                return TMacroValues::TXString{fn(s.Data)};
            },
            [&](const TMacroValues::TXStrings& ss) -> TMacroValues::TValue {
                TMacroValues::TXStrings result;
                result.Data.reserve(ss.Data.size());
                for (auto& s : ss.Data)
                    result.Data.push_back(fn(s));
                return result;
            },
            [&](const auto& x) -> TMacroValues::TValue {
                throw TBadArgType(name, x);
            }
        }, v);
    }

    template <class F>
    TTermValue MapEvalString(TStringBuf name, const TTermValue& v, F&& fn) {
        // fn: TString -> TString
        return std::visit(TOverloaded{
            [](TTermError) -> TTermValue {
                Y_ABORT();
            },
            [](TTermNothing) -> TTermValue {
                return TTermNothing();
            },
            [&](const TString& s) -> TTermValue {
                return fn(s);
            },
            [&](const TVector<TString>& ss) -> TTermValue {
                TVector<TString> result;
                result.reserve(ss.size());
                for (auto& s : ss)
                    result.push_back(fn(s));
                return std::move(result);
            },
            [&](const TTaggedStrings& x) -> TTermValue {
                throw TBadArgType(name, x);
            }
        }, v);
    }

    template <class F>
    TMacroValues::TValue ExpandPreString(TStringBuf name, const TMacroValues::TValue& v, F&& fn) {
        // fn: std::string -> std::vector<std::string>
        return std::visit(TOverloaded{
            [](std::monostate) -> TMacroValues::TValue {
                return std::monostate();
            },
            [&](const TMacroValues::TXString& s) -> TMacroValues::TValue {
                return TMacroValues::TXStrings{fn(s.Data)};
            },
            [&](const TMacroValues::TXStrings& ss) -> TMacroValues::TValue {
                TMacroValues::TXStrings result;
                for (auto& s : ss.Data) {
                    auto pieces = fn(s);
                    for (auto& p : pieces)
                        result.Data.push_back(std::move(p));
                }
                return result;
            },
            [&](const auto& x) -> TMacroValues::TValue {
                throw TBadArgType(name, x);
            }
        }, v);
    }

    template <class F>
    TTermValue ExpandEvalString(TStringBuf name, const TTermValue& v, F&& fn) {
        // fn: TString -> TVector<TString>
        return std::visit(TOverloaded{
            [](TTermError) -> TTermValue {
                Y_ABORT();
            },
            [](TTermNothing) -> TTermValue {
                return TTermNothing();
            },
            [&](const TString& s) -> TTermValue {
                return fn(s);
            },
            [&](const TVector<TString>& ss) -> TTermValue {
                TVector<TString> result;
                for (auto& s : ss)
                    for (auto& p : fn(s))
                        result.push_back(std::move(p));
                return std::move(result);
            },
            [&](const TTaggedStrings& x) -> TTermValue {
                throw TBadArgType(name, x);
            }
        }, v);
    }

    //
    // pre: place arg in front of the body
    //   Usage: ${pre=PREFIX:STR}
    //

    class TPre: public TBasicModImpl {
    public:
        TPre(): TBasicModImpl({.Id = EMacroFunction::Pre, .Name = "pre", .Arity = 2}) {
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
    // suf: append arg to the body element-wise.
    //   Usage: ${suf=SUFFIX:STR}
    //

    class TSuf: public TBasicModImpl {
    public:
        TSuf(): TBasicModImpl({.Id = EMacroFunction::Suf, .Name = "suf", .Arity = 2}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            const auto suf = std::get<TMacroValues::TXString>(args[0]).Data;
            return MapPreString(Name, args[1], [&](const std::string& body) {
                return body + suf;
            });
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            const auto suffix = std::get<TString>(args[0]);
            return MapEvalString(Name, args[1], [&](const TString& body) {
                return body + suffix;
            });
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    // join: concatenate list elements with a glue.
    //   Usage: ${join=GLUE:STR}
    //

    class TJoin: public TBasicModImpl {
    public:
        TJoin(): TBasicModImpl({.Id = EMacroFunction::Join, .Name = "join", .Arity = 2}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            const auto glue = std::get<TMacroValues::TXString>(args[0]).Data;
            return std::visit(TOverloaded{
                [](std::monostate) -> TMacroValues::TValue {
                    return std::monostate();
                },
                [](const TMacroValues::TXString& body) -> TMacroValues::TValue {
                    return body;
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    if (bodies.Data.empty())
                        return std::monostate();
                    return TMacroValues::TXString{JoinSeq(TString(glue), bodies.Data)};
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
            const auto glue = std::get<TString>(args[0]);
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
    // toupper / tolower: case-conversion modifiers.
    //

    class TToUpper: public TBasicModImpl {
    public:
        TToUpper(): TBasicModImpl({.Id = EMacroFunction::ToUpper, .Name = "toupper", .Arity = 1}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return MapEvalString(Name, args[0], [](TString s) {
                s.to_upper();
                return s;
            });
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    class TToLower: public TBasicModImpl {
    public:
        TToLower(): TBasicModImpl({.Id = EMacroFunction::ToLower, .Name = "tolower", .Arity = 1}) {
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            return MapEvalString(Name, args[0], [](TString s) {
                s.to_lower();
                return s;
            });
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    // quo / qe: legacy no-op markers for the arg-centric model.
    //

    class TQuo: public TBasicModImpl {
    public:
        TQuo(): TBasicModImpl({.Id = EMacroFunction::Quo, .Name = "quo", .Arity = 1}) {
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

    class TQuoteEach: public TBasicModImpl {
    public:
        TQuoteEach(): TBasicModImpl({.Id = EMacroFunction::QuoteEach, .Name = "qe", .Arity = 1}) {
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
    // hash: MD5 digest of the input value.
    //

    namespace {
        MD5 Md5Beg(const TStringBuf& arg) {
            MD5 md5;
            md5.Update(arg);
            return md5;
        }
        std::string Md5End(MD5& md5) {
            char buffer[33]; // MD5 class requires a 33-byte buffer
            md5.End(buffer);
            return std::string{buffer};
        }
        TString HashString(const auto& s) {
            auto md5 = Md5Beg(s);
            return Md5End(md5);
        }
        TString HashStrings(const auto& ss) {
            static const TStringBuf separator = "|";
            // init/finalize with a separator so digest of empty string differs from empty array
            auto md5 = Md5Beg(separator);
            for (const auto& s : ss) {
                md5.Update(s);
                md5.Update(separator);
            }
            return Md5End(md5);
        }
    }

    class THash: public TBasicModImpl {
    public:
        THash(): TBasicModImpl({.Id = EMacroFunction::Hash, .Name = "hash", .Arity = 1}) {
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
                    return TMacroValues::TXString{HashString(body.Data)};
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    return TMacroValues::TXString{HashStrings(bodies.Data)};
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
                    return HashString(body);
                },
                [&](const TVector<TString>& bodies) -> TTermValue {
                    return HashStrings(bodies);
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[0]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    // split: split a string by a separator into a list of strings.
    //   Usage: ${split=SEPARATOR:STR}
    // The inverse of "join" when the same SEPARATOR is used (provided SEPARATOR
    // is non-empty).  An empty SEPARATOR is treated as "do not split" and yields
    // a single-element list whose only element is the original body.  Applied to
    // a list-typed input, the modifier is applied element-wise and the results
    // are concatenated.
    //

    namespace {
        // Single template that covers both TString and std::string; the Preevaluate
        // path needs std::vector<std::string>, so the template defaults to TVector
        // and is overridden at the std::string call site.
        template <class TStr, template <class...> class TVec = TVector>
        TVec<TStr> SplitBySeparator(const TStr& separator, const TStr& body) {
            TVec<TStr> result;
            if (separator.empty()) {
                result.push_back(body);
                return result;
            }
            size_t start = 0;
            size_t pos = body.find(separator);
            while (pos != TStr::npos) {
                result.emplace_back(body.substr(start, pos - start));
                start = pos + separator.size();
                pos = body.find(separator, start);
            }
            result.emplace_back(body.substr(start));
            return result;
        }
    }

    class TSplit: public TBasicModImpl {
    public:
        TSplit(): TBasicModImpl({.Id = EMacroFunction::Split, .Name = "split", .Arity = 2}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            const auto separator = std::get<TMacroValues::TXString>(args[0]).Data;
            return ExpandPreString(Name, args[1], [&](const std::string& body) {
                return SplitBySeparator<std::string, std::vector>(separator, body);
            });
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            const auto separator = std::get<TString>(args[0]);
            return ExpandEvalString(Name, args[1], [&](const TString& body) {
                return SplitBySeparator<TString>(separator, body);
            });
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    //
    // at: extract a single element from a list by index (Python-like, negatives allowed).
    //   Usage: ${at=N:LIST}
    // A scalar input is treated as a 1-element list.  An out-of-range index
    // (including negative indices smaller than -size) yields no value
    // (monostate / TTermNothing).
    //

    namespace {
        bool ResolveIndex(ssize_t idx, size_t size, size_t& out) {
            if (size == 0)
                return false;
            if (idx < 0)
                idx += static_cast<ssize_t>(size);
            if (idx < 0 || idx >= static_cast<ssize_t>(size))
                return false;
            out = static_cast<size_t>(idx);
            return true;
        }
        ssize_t ParseSignedIndex(TStringBuf s, TStringBuf modName) {
            ssize_t v = 0;
            if (!TryFromString<ssize_t>(s, v))
                throw yexception() << modName << ": bad index value '" << s << "'";
            return v;
        }
    }

    class TAt: public TBasicModImpl {
    public:
        TAt(): TBasicModImpl({.Id = EMacroFunction::At, .Name = "at", .Arity = 2}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            const ssize_t idx = ParseSignedIndex(std::get<TMacroValues::TXString>(args[0]).Data, Name);
            return std::visit(TOverloaded{
                [](std::monostate) -> TMacroValues::TValue {
                    return std::monostate();
                },
                [&](const TMacroValues::TXString& body) -> TMacroValues::TValue {
                    size_t pos;
                    if (!ResolveIndex(idx, 1, pos))
                        return std::monostate();
                    return body;
                },
                [&](const TMacroValues::TXStrings& bodies) -> TMacroValues::TValue {
                    size_t pos;
                    if (!ResolveIndex(idx, bodies.Data.size(), pos))
                        return std::monostate();
                    return TMacroValues::TXString{bodies.Data[pos]};
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
            const ssize_t idx = ParseSignedIndex(std::get<TString>(args[0]), Name);
            return std::visit(TOverloaded{
                [](TTermError) -> TTermValue {
                    Y_ABORT();
                },
                [](TTermNothing) -> TTermValue {
                    return TTermNothing();
                },
                [&](const TString& body) -> TTermValue {
                    size_t pos;
                    if (!ResolveIndex(idx, 1, pos))
                        return TTermNothing();
                    return body;
                },
                [&](const TVector<TString>& bodies) -> TTermValue {
                    size_t pos;
                    if (!ResolveIndex(idx, bodies.size(), pos))
                        return TTermNothing();
                    return bodies[pos];
                },
                [&](const TTaggedStrings& x) -> TTermValue {
                    throw TBadArgType(Name, x);
                }
            }, args[1]);
        }
    } Y_GENERATE_UNIQUE_ID(Mod);

    // trim: strip any of the given characters from both ends of a string.
    //   Usage: ${trim=CHARS:STR}
    //
    // Semantics:
    //   * the argument is a single positional string of characters; if it is
    //     empty (e.g. ${trim:S}), the default set " \t\n\r" is used;
    //   * any leading/trailing byte that belongs to the set is removed; the
    //     scan stops at the first byte that is not in the set;
    //   * works byte-wise (not Unicode-aware);
    //   * applied to a list-typed input, the modifier is applied element-wise.
    //
    class TTrim: public TBasicModImpl {
    public:
        TTrim(): TBasicModImpl({.Id = EMacroFunction::Trim, .Name = "trim", .Arity = 2}) {
        }
        TMacroValues::TValue Preevaluate(
            [[maybe_unused]] const TPreevalCtx& ctx,
            [[maybe_unused]] std::span<TMacroValues::TValue> args
        ) const override {
            CheckArgCount(args);
            const std::string chars(std::get<TMacroValues::TXString>(args[0]).Data);
            return MapPreString(Name, args[1], [&](const std::string& body) {
                return ApplyOne(chars, body);
            });
        }
        TTermValue Evaluate(
            [[maybe_unused]] std::span<const TTermValue> args,
            [[maybe_unused]] const TEvalCtx& ctx,
            [[maybe_unused]] ICommandSequenceWriter* writer
        ) const override {
            CheckArgCount(args);
            const std::string chars(std::get<TString>(args[0]));
            return MapEvalString(Name, args[1], [&](const TString& body) {
                return ApplyOne(chars, body);
            });
        }
    private:
        template <class TStr>
        static TStr ApplyOne(const std::string& chars, TStr body) {
            if (body.empty())
                return body;
            const std::string& set = chars.empty() ? DefaultChars() : chars;
            size_t start = 0;
            while (start < body.size() && set.find(body[start]) != std::string::npos)
                ++start;
            size_t end = body.size();
            while (end > start && set.find(body[end - 1]) != std::string::npos)
                --end;
            return body.substr(start, end - start);
        }
        static const std::string& DefaultChars() {
            static const std::string kDefault(" \t\n\r");
            return kDefault;
        }
    } Y_GENERATE_UNIQUE_ID(Mod);
}
