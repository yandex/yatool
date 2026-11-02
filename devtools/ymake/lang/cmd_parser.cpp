#include "cmd_parser.h"
#include "parser_lexer.h"

#include <devtools/ymake/commands/mod_registry.h>
#include <devtools/ymake/conf.h>
#include <devtools/ymake/libs/polexpr/variadic_builder.h>

#include <util/generic/overloaded.h>
#include <util/generic/scope.h>

#include <optional>

using namespace NCommands;
using namespace NYMake::NParsing;

namespace {
    class TCommandParser {
    public:
        TCommandParser(const TBuildConfiguration* conf, const TModRegistry& mods, TMacroValues& values, TStringBuf source)
            : Conf(conf)
            , Mods(mods)
            , Values(values)
            , Lexer(source)
        {
        }

        TSyntax Parse() {
            TSyntax result;
            SkipSpaces();
            while (Accept(EToken::CommandSeparator)) {
                SkipSpaces();
            }
            while (Peek().Kind != EToken::End) {
                auto& command = result.Script.emplace_back();
                for (;;) {
                    command.push_back(ParseArgument(false));
                    const bool separated = SkipSpaces();
                    if (Peek().Kind == EToken::End || Peek().Kind == EToken::CommandSeparator) {
                        break;
                    }
                    if (!separated) {
                        Error("expected an argument separator");
                    }
                }
                while (Accept(EToken::CommandSeparator)) {
                    SkipSpaces();
                }
            }
            return result;
        }

    private:
        const TToken& Peek() {
            if (!Token) {
                Token = Lexer.Next();
            }
            return *Token;
        }

        TToken Take() {
            auto token = Peek();
            Token.reset();
            return token;
        }

        bool Accept(EToken kind) {
            if (Peek().Kind != kind) {
                return false;
            }
            Take();
            return true;
        }

        void Expect(EToken kind) {
            if (!Accept(kind)) {
                Error("unexpected token");
            }
        }

        bool SkipSpaces() {
            return Accept(EToken::Space);
        }

        [[noreturn]] void Error(TStringBuf message) {
            const auto& token = Peek();
            ythrow yexception() << "could not parse command (" << token.Line << ':' << token.Column << "): " << message << '\n'
                                << Lexer.Source();
        }

        TSyntax::TArgument ParseArgument(bool callArgument) {
            TSyntax::TArgument argument;
            for (;;) {
                const auto kind = Peek().Kind;
                if (kind == EToken::Space || kind == EToken::CommandSeparator || kind == EToken::End ||
                    (callArgument && kind == EToken::RightParen)) {
                    break;
                }
                if (callArgument && kind == EToken::LeftParen) {
                    Error("unexpected '(' in a macro argument");
                }
                switch (kind) {
                    case EToken::Raw:
                    case EToken::Noop:
                    case EToken::LeftParen:
                    case EToken::RightParen: {
                        std::string text;
                        do {
                            text += Take().Text;
                            const auto next = Peek().Kind;
                            if (next != EToken::Raw && next != EToken::Noop &&
                                (callArgument || (next != EToken::LeftParen && next != EToken::RightParen))) {
                                break;
                            }
                        } while (true);
                        if (MacroCallDepth) {
                            argument.emplace_back(TSyntax::TIdOrString{UnescapeRaw(text)});
                        } else {
                            argument.emplace_back(TMacroValues::TXString{UnescapeRaw(text)});
                        }
                        break;
                    }
                    case EToken::Variable: {
                        auto name = Take().Text.SubStr(1);
                        if (Peek().Kind == EToken::LeftParen && CallAhead()) {
                            Take();
                            argument.emplace_back(ParseCall(name));
                        } else {
                            argument.emplace_back(Values.InsertVar(name));
                        }
                        break;
                    }
                    case EToken::EvaluationBegin:
                        Take();
                        argument.emplace_back(ParseTransformation());
                        break;
                    case EToken::SingleQuote:
                    case EToken::DoubleQuote:
                        ParseQuoted(argument, kind);
                        break;
                    default:
                        Error("expected an argument term");
                }
            }
            if (argument.empty()) {
                Error("expected an argument");
            }
            return argument;
        }

        void ParseQuoted(TSyntax::TArgument& argument, EToken quote) {
            Take();
            while (!Accept(quote)) {
                switch (Peek().Kind) {
                    case EToken::Raw:
                    case EToken::Noop:
                        argument.emplace_back(TMacroValues::TXString{UnescapeQuoted(Take().Text)});
                        break;
                    case EToken::Variable:
                        argument.emplace_back(Values.InsertVar(Take().Text.SubStr(1)));
                        break;
                    case EToken::EvaluationBegin:
                        Take();
                        argument.emplace_back(ParseTransformation());
                        break;
                    default:
                        Error("expected a quoted term or a closing quote");
                }
            }
            bool appendEmpty = argument.empty();
            if (argument.size() == 1) {
                auto value = std::get_if<TMacroValues::TValue>(&argument.front());
                appendEmpty = !(value && std::holds_alternative<TMacroValues::TXString>(*value));
            }
            if (appendEmpty) {
                argument.emplace_back(TMacroValues::TXString{});
            }
        }

        TSyntax::TTransformation ParseTransformation() {
            TSyntax::TTransformation result;
            if (Peek().Kind == EToken::Identifier) {
                auto name = Take().Text;
                if (Accept(EToken::EvaluationEnd)) {
                    result.Body = {{Values.InsertVar(name)}};
                    return result;
                }
                for (;;) {
                    auto id = Mods.TryGetId(name);
                    if (!id) {
                        ythrow yexception() << "unknown modifier " << name;
                    }
                    auto& modifier = result.Mods.emplace_back(TSyntax::TTransformation::TModifier{*id, {}});
                    if (Accept(EToken::ModifierArgument)) {
                        auto& argument = modifier.Arguments.emplace_back();
                        for (;;) {
                            switch (Peek().Kind) {
                                case EToken::ModifierText:
                                    argument.emplace_back(TMacroValues::TXString{UnescapeRaw(Take().Text)});
                                    continue;
                                case EToken::ModifierVariable:
                                    argument.emplace_back(Values.InsertVar(Take().Text.SubStr(1)));
                                    continue;
                                case EToken::ModifierEvaluation: {
                                    const auto text = Take().Text;
                                    argument.emplace_back(Values.InsertVar(text.SubStr(2, text.size() - 3)));
                                    continue;
                                }
                                default:
                                    break;
                            }
                            break;
                        }
                    }
                    if (!Accept(EToken::ModifierSeparator)) {
                        break;
                    }
                    if (Peek().Kind != EToken::Identifier) {
                        Error("expected a modifier name");
                    }
                    name = Take().Text;
                }
                Expect(EToken::ModifierEnd);
            }
            if (Peek().Kind == EToken::Identifier) {
                result.Body = {{Values.InsertVar(Take().Text)}};
            } else if (Peek().Kind == EToken::String) {
                result.Body = {{TMacroValues::TXString{UnquoteDouble(Take().Text)}}};
            } else {
                Error("expected a transformation body");
            }
            Expect(EToken::EvaluationEnd);
            return result;
        }

        bool CallAhead() {
            // A variable followed by '(' can still be plain text. ANTLR chooses
            // the call alternative only when the complete argument list fits.
            auto scanner = Lexer;
            size_t depth = 1;
            EToken previous = EToken::LeftParen;
            for (;;) {
                const auto token = scanner.Next();
                if (token.Kind == EToken::End || token.Kind == EToken::CommandSeparator) {
                    return false;
                }
                if (token.Kind == EToken::LeftParen) {
                    if (previous != EToken::Variable) {
                        return false;
                    }
                    if (++depth > 256) {
                        Error("macro call nesting limit exceeded");
                    }
                } else if (token.Kind == EToken::RightParen && --depth == 0) {
                    return true;
                }
                previous = token.Kind;
            }
        }

        TSyntax::TTerm ParseCall(TStringBuf macroName) {
            TSyntax::TCommand rawArgs;
            {
                if (++MacroCallDepth > 256) {
                    Error("macro call nesting limit exceeded");
                }
                Y_DEFER {
                    --MacroCallDepth;
                };
                SkipSpaces();
                if (!Accept(EToken::RightParen)) {
                    for (;;) {
                        rawArgs.push_back(ParseArgument(true));
                        const bool separated = SkipSpaces();
                        if (Accept(EToken::RightParen)) {
                            break;
                        }
                        if (!separated) {
                            Error("expected a macro argument separator");
                        }
                    }
                }
            }
            if (macroName == "IF") {
                static const auto ifBlockData = [] {
                    auto keywords = TSignature::TKeywords();
                    keywords.AddArrayKeyword("THEN", "");
                    keywords.AddArrayKeyword("ELSE", "");
                    auto blockData = TBlockData{};
                    blockData.Completed = true;
                    blockData.IsUserMacro = true;
                    const TVector<TString> positionalArgs = {"COND"};
                    blockData.CmdProps.Reset(new TCmdProperty{positionalArgs, std::move(keywords)});
                    return blockData;
                }();
                auto args = CollectArgs(macroName, ifBlockData.CmdProps->Signature(), std::move(rawArgs));
                auto& cond = FindCallArg(args, "COND");
                auto& then = FindCallArg(args, "THEN");
                auto& otherwise = FindCallArg(args, "ELSE");
                if (!(cond.Script.size() == 1 && cond.Script.front().size() == 1 && cond.Script.front().front().size() == 1)) {
                    ythrow TError() << "Bad condition shape";
                }
                if (then.Script.size() != 1) {
                    ythrow TError() << "Bad then-branch shape";
                }
                if (otherwise.Script.size() != 1) {
                    ythrow TError() << "Bad else-branch shape";
                }
                return TSyntax::TBuiltinIf{.Cond = MakeSimpleShared<TSyntax::TTerm>(cond.Script.front().front().front()),
                                           .Then = std::move(then.Script.front()),
                                           .Else = std::move(otherwise.Script.front())};
            }
            if (!Conf) {
                ythrow TError() << "Macro " << macroName << " not found";
            }
            auto it = Conf->BlockData.find(macroName);
            if (it == Conf->BlockData.end()) {
                ythrow TError() << "Macro " << macroName << " not found";
            }
            return CollectArgs(macroName, it->second.CmdProps->Signature(), std::move(rawArgs));
        }

        const TSyntax& FindCallArg(const TSyntax::TCall& call, TStringBuf name) {
            auto it = std::find(call.ArgumentNames.begin(), call.ArgumentNames.end(), name);
            if (it == call.ArgumentNames.end()) [[unlikely]] {
                ythrow TError() << "call argument " << name << " expected";
            }
            return call.Arguments[it - call.ArgumentNames.begin()];
        }

        TSyntax::TCall CollectArgs(TStringBuf macroName, const TSignature& signature, TSyntax::TCommand rawArgs) {
            // see the "functions/arg-passing" test for the sorts of patterns this logic is supposed to handle

            //
            // block data representations:
            // `macro FOOBAR(A, B[], C...) {...}`  -->  ArgNames == {"B...", "A", "C..."}, Keywords = {"B"}
            //
            // cf. ConvertArgsToPositionalArrays() and MapMacroVars()
            //
            // the resulting collection follows the ordering in ArgNames
            //
            auto args = TVector<TSyntax>(signature.ArgNames().size());
            const TKeyword* kwDesc = nullptr;

            auto kwArgCnt = signature.GetKeyArgsNum();
            auto posArgCnt = signature.ArgNames().size() - signature.GetKeyArgsNum();
            auto hasVarArg = posArgCnt != 0 && signature.ArgNames().back().EndsWith(NStaticConf::ARRAY_SUFFIX);

            for (size_t i = 0; i != posArgCnt; ++i) {
                args[kwArgCnt + i].Script.emplace_back();
            }
            if (hasVarArg) {
                --posArgCnt;
            }

            auto maybeStartNamedArg = [&]() {
                Y_ASSERT(kwDesc);
                auto namedArg = &args[kwDesc->Pos];
                if (kwDesc->Kind == TKeyword::Flag) {
                    return;
                }
                if (namedArg->Script.empty()) {
                    namedArg->Script.emplace_back();
                }
            };

            auto maybeFinishNamedArg = [&]() {
                if (!kwDesc) {
                    return;
                }
                auto namedArg = &args[kwDesc->Pos];
                if (kwDesc->Kind == TKeyword::Flag) {
                    if (namedArg->Script.empty()) { // it will not be empty if we repeat the respective arg; TBD do we even want to allow this?
                        AssignPreset(namedArg->Script, kwDesc->OnKwPresent);
                    }
                    kwDesc = nullptr;
                    return;
                }
                if (namedArg->Script.back().size() == kwDesc->Arity()) {
                    kwDesc = nullptr;
                }
            };

            size_t posArg = 0;
            for (auto rawArg = rawArgs.begin(); rawArg != rawArgs.end(); ++rawArg, maybeFinishNamedArg()) {
                if (rawArg->size() == 1) {
                    if (auto kw = std::get_if<TSyntax::TIdOrString>(&rawArg->front())) {
                        if (auto* keywordData = signature.GetKeywordData(kw->Value)) {
                            kwDesc = keywordData;
                            maybeStartNamedArg();
                            continue;
                        }
                    }
                }

                for (auto& term : *rawArg) {
                    if (auto str = std::get_if<TSyntax::TIdOrString>(&term)) {
                        term = TMacroValues::TXString{str->Value};
                    }
                }

                if (kwDesc) {
                    auto namedArg = &args[kwDesc->Pos];
                    namedArg->Script.back().push_back(std::move(*rawArg));
                } else {
                    if (posArg < posArgCnt) {
                        args[kwArgCnt + posArg].Script.back().push_back(std::move(*rawArg));
                    } else if (hasVarArg) {
                        args[kwArgCnt + posArgCnt].Script.back().push_back(std::move(*rawArg));
                    } else {
                        throw TError()
                            << "Macro " << macroName
                            << " called with too many positional arguments"
                            << " (" << posArgCnt << " expected)";
                    }
                    ++posArg;
                }
            }

            if (posArg < posArgCnt) {
                throw TError()
                    << "Macro " << macroName
                    << " called with too few positional arguments"
                    << " (" << posArgCnt << (hasVarArg ? " or more" : "") << " expected)";
            }

            for (const auto& [name, kw] : signature.GetKeywords()) {
                auto namedArg = &args[kw.Pos];
                if (!namedArg->Script.empty()) {
                    Y_ASSERT(namedArg->Script.size() == 1);
                    if (kw.Kind == TKeyword::Scalar && namedArg->Script.back().empty()) {
                        throw TError()
                            << "Macro " << macroName
                            << " did not get enough data for the named argument " << name;
                    }
                }
                if (namedArg->Script.empty()) {
                    AssignPreset(namedArg->Script, kw.OnKwMissing);
                }
            }

            auto argNames = std::vector<TStringBuf>();
            argNames.reserve(signature.ArgNames().size());
            for (auto& name : signature.ArgNames()) {
                argNames.push_back(name);
                if (argNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX)) {
                    argNames.back().Chop(strlen(NStaticConf::ARRAY_SUFFIX));
                }
            }

            Y_ASSERT(args.size() == argNames.size());
            return TSyntax::TCall{
                .Function = Values.InsertVar(macroName),
                .Arguments = std::move(args),
                .ArgumentNames = std::move(argNames)};
        }

        void AssignPreset(TSyntax::TScript& dst, const TVector<TString>& src) {
            Y_ASSERT(dst.empty());
            TSyntax::TArgument arg;
            arg.reserve(src.size());
            for (auto& s : src) {
                arg.emplace_back(TMacroValues::TXString{s});
            }
            dst.push_back({std::move(arg)});
        }

        std::string Unvariable(std::string_view s) {
            if (!(s.size() >= 2 && s.front() == '$')) {
                throw yexception() << "bad variable name " << s;
            }
            return std::string(s.substr(1, s.size() - 1));
        }

        std::string UnquoteDouble(std::string_view s) {
            if (!(s.size() >= 2 && s.front() == '"' && s.back() == '"')) {
                throw yexception() << "bad string " << s;
            }
            return UnescapeQuoted(s.substr(1, s.size() - 2));
        }

        std::string UnescapeQuoted(std::string_view s) {
            std::string result;
            result.reserve(s.size());
            for (size_t i = 0; i != s.size(); ++i) {
                if (s[i] == '\\') {
                    if (++i == s.size()) {
                        throw yexception() << "incomplete escape sequence in " << s;
                    }
                    auto c = s[i];
                    if (!(c == '\'' || c == '"' || c == '\\' || c == '/')) { // see `IsValidEscapeSym`
                        result += '\\';
                    }
                }
                result += s[i];
            }
            return result;
        }

        std::string UnescapeRaw(std::string_view s) {
            std::string result;
            result.reserve(s.size());
            for (size_t i = 0; i != s.size(); ++i) {
                if (s[i] == '\\') {
                    if (++i == s.size()) {
                        throw yexception() << "incomplete escape sequence in " << s;
                    }
                }
                result += s[i];
            }
            return result;
        }

        const TBuildConfiguration* Conf;
        const TModRegistry& Mods;
        TMacroValues& Values;
        TLexer Lexer;
        std::optional<TToken> Token;
        size_t MacroCallDepth = 0;
    };
} // namespace
TSyntax NCommands::Parse(const TBuildConfiguration* conf, const TModRegistry& mods, TMacroValues& values, TStringBuf src) {
    try {
        return TCommandParser(conf, mods, values, src).Parse();
    } catch (const TLexerError& error) {
        ythrow yexception() << "could not parse command: " << error.what() << '\n'
                            << src;
    }
}

namespace {

    void CompileTerm(const TModRegistry& mods, const TSyntax::TTerm& term, NPolexpr::TVariadicCallBuilder& termsBuilder, TMacroValues& values);
    void CompileArgs(const TModRegistry& mods, const TSyntax::TCommand& cmd, NPolexpr::TVariadicCallBuilder& cmdsBuilder, TMacroValues& values);

    void CompileTransformation(
        const TModRegistry& mods,
        const TSyntax::TTransformation& x,
        NPolexpr::TVariadicCallBuilder& builder,
        TMacroValues& values,
        size_t pos = 0
    ) {
        if (pos == x.Mods.size()) {
            if (x.Body.size() == 1 && x.Body.front().size() == 1)
                // this special case is here mostly to reinforce the notion
                // that `${VAR}` should be equivalent to `$VAR`;
                // we use it with `${mods:VAR}`, as well,
                // to cut down on `Args(Terms(...))` wrappers;
                // conceptually, with proper typing support,
                // this should not be required
                CompileTerm(mods, x.Body.front().front(), builder, values);
            else
                CompileArgs(mods, x.Body, builder, values);
            return;
        }

        auto& m = x.Mods[pos];
        auto func = m.Function;
        auto arity = mods.FuncArity(func);
        if (arity != 0 && arity != m.Arguments.size() + 1)
            throw yexception()
                << "bad modifier argument count for " << m.Function
                << " (expected " << mods.FuncArity(func) - 1
                << ", given " << m.Arguments.size()
                << ")";
        auto doTheArgs = [&](auto& builder) {
            for (auto& v : m.Arguments) {
                if (v.size() == 1)
                    CompileTerm(mods, v[0], builder, values);
                else {
                    NPolexpr::TVariadicCallBuilder catBuilder(builder, mods.Func2Id(EMacroFunction::Cat));
                    for (auto& t : v)
                        CompileTerm(mods, t, catBuilder, values);
                    catBuilder.Build<EMacroFunction>();
                }
            }
            CompileTransformation(mods, x, builder, values, pos + 1);
        };
        if (arity == 0) {
            NPolexpr::TVariadicCallBuilder modBuilder(builder, mods.Func2Id(func));
            doTheArgs(modBuilder);
            modBuilder.Build<EMacroFunction>();
        } else {
            builder.Append(mods.Func2Id(func));
            doTheArgs(builder);
        }
    }

    void CompileTerm(const TModRegistry& mods, const TSyntax::TTerm& term, NPolexpr::TVariadicCallBuilder& termsBuilder, TMacroValues& values) {
        std::visit(TOverloaded{
            [&](const TMacroValues::TValue& s) {
                termsBuilder.Append(values.InsertValue(s));
            },
            [&](NPolexpr::EVarId v) {
                termsBuilder.Append(v);
            },
            [&](const TSyntax::TTransformation& x) {
                CompileTransformation(mods, x, termsBuilder, values);
            },
            [&](const TSyntax::TCall&) {
                Y_ABORT();
            },
            [&](const TSyntax::TBuiltinIf&) {
                Y_ABORT();
            },
            [&](const TSyntax::TIdOrString&) {
                Y_ABORT();
            },
            [&](const TSyntax::TUnexpanded&) {
                Y_ABORT();
            },
        }, term);
    }

    void CompileArgs(const TModRegistry& mods, const TSyntax::TCommand& cmd, NPolexpr::TVariadicCallBuilder& cmdsBuilder, TMacroValues& values) {
        NPolexpr::TVariadicCallBuilder argsBuilder(cmdsBuilder, mods.Func2Id(EMacroFunction::Args));
        for (auto& arg : cmd) {
            NPolexpr::TVariadicCallBuilder termsBuilder(argsBuilder, mods.Func2Id(EMacroFunction::Terms));
            for (auto& term : arg)
                CompileTerm(mods, term, termsBuilder, values);
            termsBuilder.Build<EMacroFunction>();
        }
        argsBuilder.Build<EMacroFunction>();
    }

}

NPolexpr::TExpression NCommands::Compile(const TModRegistry& mods, const TSyntax& s, TMacroValues& values) {
    NPolexpr::TExpression result;
    NPolexpr::TVariadicCallBuilder cmdsBuilder(result, mods.Func2Id(EMacroFunction::Cmds));
    for (size_t cmd = 0; cmd != s.Script.size(); ++cmd)
        CompileArgs(mods, s.Script[cmd], cmdsBuilder, values);
    cmdsBuilder.Build<EMacroFunction>();
    return result;
}
