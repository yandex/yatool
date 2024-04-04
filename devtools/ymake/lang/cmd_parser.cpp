#include "cmd_parser.h"

#include <devtools/ymake/lang/CmdLexer.h>
#include <devtools/ymake/lang/CmdParserBaseVisitor.h>

#include <devtools/ymake/polexpr/variadic_builder.h>

#include <util/generic/overloaded.h>

using namespace NCommands;

namespace {

    //
    //
    //

    class TCmdParserVisitor_Polexpr: public CmdParserBaseVisitor {

    public:

        TCmdParserVisitor_Polexpr(TMacroValues& values)
            : Values(values)
        {
        }

        auto Extract() {
            return std::exchange(Syntax, {});
        }

    public: // CmdParserBaseVisitor

        // structural elements

        std::any visitCmd(CmdParser::CmdContext *ctx) override {
            Syntax.Commands.emplace_back();
            return visitChildren(ctx);
        }

        std::any visitArg(CmdParser::ArgContext *ctx) override {
            Syntax.Commands.back().emplace_back();
            return visitChildren(ctx);
        }

        // plaintext terms

        std::any visitTermR(CmdParser::TermRContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermV(CmdParser::TermVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermS(CmdParser::TermSContext *ctx) override { return doVisitTermS(ctx); }

        // single-quoted terms

        std::any visitTermSQR(CmdParser::TermSQRContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermSQV(CmdParser::TermSQVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermSQS(CmdParser::TermSQSContext *ctx) override { return doVisitTermS(ctx); }

        // double-quoted terms

        std::any visitTermDQR(CmdParser::TermDQRContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermDQV(CmdParser::TermDQVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermDQS(CmdParser::TermDQSContext *ctx) override { return doVisitTermS(ctx); }

        // substitution pieces

        std::any visitSubModKey(CmdParser::SubModKeyContext *ctx) override {
            GetCurrentSubst().Mods.push_back({ctx->getText(), {}});
            return visitChildren(ctx);
        }

        std::any visitSubModValue(CmdParser::SubModValueContext *ctx) override {
            GetCurrentSubst().Mods.back().Values.emplace_back();
            return visitChildren(ctx);
        }

        std::any visitSubModValueT(CmdParser::SubModValueTContext *ctx) override {
            GetCurrentSubst().Mods.back().Values.back().push_back(Values.InsertStr(ctx->getText()));
            return visitChildren(ctx);
        }

        std::any visitSubModValueV(CmdParser::SubModValueVContext *ctx) override {
            GetCurrentSubst().Mods.back().Values.back().push_back(Values.InsertVar(Unvariable(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any visitSubModValueE(CmdParser::SubModValueEContext *ctx) override {
            std::string s = ctx->getText();
            if (!(s.size() > 3 && s.starts_with("${") && s.ends_with("}")))
                throw yexception() << "bad variable name " << s;
            std::string_view sv(s.begin() + 2, s.end() - 1);
            GetCurrentSubst().Mods.back().Values.back().push_back(Values.InsertVar(sv));
            return visitChildren(ctx);
        }

        std::any visitSubBodyIdentifier(CmdParser::SubBodyIdentifierContext *ctx) override {
            GetCurrentSubst().Body = {{Values.InsertVar(ctx->getText())}};
            return visitChildren(ctx);
        }

        std::any visitSubBodyString(CmdParser::SubBodyStringContext *ctx) override {
            GetCurrentSubst().Body = {{Values.InsertStr(UnquoteDouble(ctx->getText()))}};
            return visitChildren(ctx);
        }

    private:

        std::any doVisitTermR(antlr4::RuleContext *ctx) {
            Syntax.Commands.back().back().emplace_back(Values.InsertStr(Unescape(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any doVisitTermV(antlr4::RuleContext *ctx) {
            Syntax.Commands.back().back().emplace_back(Values.InsertVar(Unvariable(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any doVisitTermS(antlr4::RuleContext *ctx) {
            Syntax.Commands.back().back().emplace_back(TSyntax::TSubstitution{});
            return visitChildren(ctx);
        }

        TSyntax::TSubstitution& GetCurrentSubst() {
            return std::get<TSyntax::TSubstitution>(Syntax.Commands.back().back().back());
        }

        std::string_view Unvariable(std::string_view s) {
            if (!(s.size() >= 2 && s.front() == '$'))
                throw yexception() << "bad variable name " << s;
            return s.substr(1, s.size() - 1);
        }

        std::string UnquoteDouble(std::string_view s) {
            if (!(s.size() >= 2 && s.front() == '"' && s.back() == '"'))
                throw yexception() << "bad string " << s;
            return Unescape(s.substr(1, s.size() - 2));
        }

        std::string Unescape(std::string_view s) {
            std::string result;
            result.reserve(s.size());
            for (size_t i = 0; i != s.size(); ++i) {
                if (s[i] == '\\') {
                    if (++i == s.size())
                        throw yexception() << "incomplete escape sequence in " << s;
                    auto c = s[i];
                    if (!(c == '\'' || c == '"' || c == '\\' || c == '/')) // see `IsValidEscapeSym`
                        result += '\\';
                }
                result += s[i];
            }
            return result;
        }

    private:

        TMacroValues& Values;
        TSyntax Syntax;

    };

    class TCmdParserErrorListener: public antlr4::BaseErrorListener {
    public:
        explicit TCmdParserErrorListener(TStringBuf cmd): Cmd(cmd) {
        }
        void syntaxError(
            antlr4::Recognizer* recognizer,
            antlr4::Token* offendingSymbol,
            size_t line,
            size_t charPositionInLine,
            const std::string& msg,
            std::exception_ptr e
        ) override {
            throw yexception() << "could not parse command: " << Cmd;
        }
    private:
        TStringBuf Cmd;
    };

}

//
//
//

TSyntax NCommands::Parse(TMacroValues& values, TStringBuf src) {

    antlr4::ANTLRInputStream input(src);
    TCmdParserErrorListener errorListener(src);

    CmdLexer lexer(&input);
    lexer.addErrorListener(&errorListener);

    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();

    CmdParser parser(&tokens);
    parser.addErrorListener(&errorListener);

    TCmdParserVisitor_Polexpr visitor(values);
    visitor.visit(parser.main());

    return visitor.Extract();

}

namespace {

    EMacroFunctions CompileFnName(TStringBuf key) {
        if (key == "hide") {
            return EMacroFunctions::Hide;
        } else if (key == "clear") {
            return EMacroFunctions::Clear;
        } else if (key == "input") {
            return EMacroFunctions::Input;
        } else if (key == "output") {
            return EMacroFunctions::Output;
        } else if (key == "tool") {
            return EMacroFunctions::Tool;
        } else if (key == "pre") {
            return EMacroFunctions::Pre;
        } else if (key == "suf") {
            return EMacroFunctions::Suf;
        } else if (key == "quo") {
            return EMacroFunctions::Quo;
        } else if (key == "noext") {
            return EMacroFunctions::CutExt;
        } else if (key == "lastext") {
            return EMacroFunctions::LastExt;
        } else if (key == "ext") {
            return EMacroFunctions::ExtFilter;
        } else if (key == "env") {
            return EMacroFunctions::SetEnv;
        } else if (key == "kv") {
            return EMacroFunctions::KeyValue;
        } else if (key == "noauto") {
            return EMacroFunctions::NoAutoSrc;
        } else {
            throw yexception() << "unknown modifier " << key;
        }
    }

    void CompileArgs(TMacroValues& values, const TSyntax::TCommand& cmd, NPolexpr::TVariadicCallBuilder& cmdsBuilder) {
        NPolexpr::TVariadicCallBuilder argsBuilder(cmdsBuilder, values.Func2Id(EMacroFunctions::Args));
        for (size_t arg = 0; arg != cmd.size(); ++arg) {
            NPolexpr::TVariadicCallBuilder termsBuilder(argsBuilder, values.Func2Id(EMacroFunctions::Terms));
            for (size_t term = 0; term != cmd[arg].size(); ++term) {
                std::visit(TOverloaded{
                    [&](NPolexpr::TConstId s) {
                        termsBuilder.Append(s);
                    },
                    [&](NPolexpr::EVarId v) {
                        termsBuilder.Append(v);
                    },
                    [&](const TSyntax::TSubstitution& s) {
                        for (auto&& m : s.Mods) {
                            auto func = CompileFnName(m.Name);
                            if (values.FuncArity(func) != m.Values.size() + 1)
                                throw yexception()
                                    << "bad modifier argument count for " << m.Name
                                    << " (expected " << values.FuncArity(func) - 1
                                    << ", given " << m.Values.size()
                                    << ")";
                            termsBuilder.Append(values.Func2Id(func));
                            for (auto&& v : m.Values) {
                                if (v.size() == 1) {
                                    std::visit(TOverloaded{
                                        [&](NPolexpr::TConstId id) {
                                            termsBuilder.Append(id);
                                        },
                                        [&](NPolexpr::EVarId id) {
                                            termsBuilder.Append(id);
                                        }
                                    }, v[0]);
                                } else {
                                    NPolexpr::TVariadicCallBuilder catBuilder(termsBuilder, values.Func2Id(EMacroFunctions::Cat));
                                    for (auto&& t : v) {
                                        std::visit(TOverloaded{
                                            [&](NPolexpr::TConstId id) {
                                                catBuilder.Append(id);
                                            },
                                            [&](NPolexpr::EVarId id) {
                                                catBuilder.Append(id);
                                            }
                                        }, t);
                                    }
                                    catBuilder.Build();
                                }
                            }
                        }
                        if (
                            s.Body.size() == 1 && s.Body.front().size() == 1 &&
                            std::holds_alternative<NPolexpr::EVarId>(s.Body.front().front())
                        ) {
                            // this special case is here mostly to reinforce the notion
                            // that `${VAR}` should be equivalent to `$VAR`;
                            // we use it with `${mods:VAR}`, as well,
                            // to cut down on `Args(Terms(...))` wrappers;
                            // conceptually, with proper typing support,
                            // this should not be required
                            termsBuilder.Append(std::get<NPolexpr::EVarId>(s.Body.front().front()));
                        } else {
                            CompileArgs(values, s.Body, termsBuilder);
                        }
                    }
                }, cmd[arg][term]);
            }
            termsBuilder.Build();
        }
        argsBuilder.Build();
    }
}

NPolexpr::TExpression NCommands::Compile(TMacroValues& values, const TSyntax& s) {
    NPolexpr::TExpression result;
    NPolexpr::TVariadicCallBuilder cmdsBuilder(result, values.Func2Id(EMacroFunctions::Cmds));
    for (size_t cmd = 0; cmd != s.Commands.size(); ++cmd)
        CompileArgs(values, s.Commands[cmd], cmdsBuilder);
    cmdsBuilder.Build();
    return result;
}
