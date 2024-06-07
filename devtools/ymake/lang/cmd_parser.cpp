#include "cmd_parser.h"

#include <devtools/ymake/lang/CmdLexer.h>
#include <devtools/ymake/lang/CmdParserBaseVisitor.h>
#include <devtools/ymake/conf.h>

#include <devtools/ymake/polexpr/variadic_builder.h>

#include <util/generic/overloaded.h>
#include <util/generic/scope.h>

using namespace NCommands;

namespace {

    EMacroFunctions CompileFnName(TStringBuf key) {
        static const THashMap<TStringBuf, EMacroFunctions> names = {
            {"hide",    EMacroFunctions::Hide},
            {"clear",   EMacroFunctions::Clear},
            {"input",   EMacroFunctions::Input},
            {"output",  EMacroFunctions::Output},
            {"tool",    EMacroFunctions::Tool},
            {"pre",     EMacroFunctions::Pre},
            {"suf",     EMacroFunctions::Suf},
            {"quo",     EMacroFunctions::Quo},
            {"noext",   EMacroFunctions::CutExt},
            {"lastext", EMacroFunctions::LastExt},
            {"ext",     EMacroFunctions::ExtFilter},
            {"env",     EMacroFunctions::SetEnv},
            {"kv",      EMacroFunctions::KeyValue},
            {"noauto",  EMacroFunctions::NoAutoSrc},
            {"glob",    EMacroFunctions::Glob},
        };
        auto it = names.find(key);
        if (it == names.end())
            throw yexception() << "unknown modifier " << key;
        return it->second;
    }

    //
    //
    //

    class TCmdParserVisitor_Polexpr: public CmdParserBaseVisitor {

    public:

        TCmdParserVisitor_Polexpr(const TBuildConfiguration* conf, TMacroValues& values)
            : Conf(conf)
            , Values(values)
        {
        }

        auto Extract() {
            return std::exchange(Syntax, {});
        }

    public: // CmdParserBaseVisitor

        // structural elements

        std::any visitCmd(CmdParser::CmdContext *ctx) override {
            Syntax.Commands.emplace_back();
            CmdStack.push_back(&Syntax.Commands.back());
            Y_DEFER {CmdStack.pop_back();};
            return visitChildren(ctx);
        }

        std::any visitArg(CmdParser::ArgContext *ctx) override {
            GetCurrentCommand().emplace_back();
            return visitChildren(ctx);
        }

        std::any visitCallArg(CmdParser::CallArgContext *ctx) override {
            GetCurrentCommand().emplace_back();
            return visitChildren(ctx);
        }

        // plaintext terms

        std::any visitTermO(CmdParser::TermOContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermA(CmdParser::TermAContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermC(CmdParser::TermCContext *ctx) override { return doVisitTermC(ctx); }
        std::any visitTermV(CmdParser::TermVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermX(CmdParser::TermXContext *ctx) override { return doVisitTermX(ctx); }

        // single-quoted terms

        std::any visitTermSQR(CmdParser::TermSQRContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermSQV(CmdParser::TermSQVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermSQX(CmdParser::TermSQXContext *ctx) override { return doVisitTermX(ctx); }

        // double-quoted terms

        std::any visitTermDQR(CmdParser::TermDQRContext *ctx) override { return doVisitTermR(ctx); }
        std::any visitTermDQV(CmdParser::TermDQVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermDQX(CmdParser::TermDQXContext *ctx) override { return doVisitTermX(ctx); }

        // transformation pieces

        std::any visitXModKey(CmdParser::XModKeyContext *ctx) override {
            GetCurrentSubst().Mods.push_back({CompileFnName(ctx->getText()), {}});
            return visitChildren(ctx);
        }

        std::any visitXModValue(CmdParser::XModValueContext *ctx) override {
            GetCurrentSubst().Mods.back().Values.emplace_back();
            return visitChildren(ctx);
        }

        std::any visitXModValueT(CmdParser::XModValueTContext *ctx) override {
            GetCurrentSubst().Mods.back().Values.back().push_back(Values.InsertStr(ctx->getText()));
            return visitChildren(ctx);
        }

        std::any visitXModValueV(CmdParser::XModValueVContext *ctx) override {
            GetCurrentSubst().Mods.back().Values.back().push_back(Values.InsertVar(Unvariable(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any visitXModValueE(CmdParser::XModValueEContext *ctx) override {
            std::string text = ctx->getText();
            if (!(text.size() > 3 && text.starts_with("${") && text.ends_with("}")))
                throw yexception() << "bad variable name " << text;
            std::string_view sv(text.begin() + 2, text.end() - 1);
            GetCurrentSubst().Mods.back().Values.back().push_back(Values.InsertVar(sv));
            return visitChildren(ctx);
        }

        std::any visitXBodyIdentifier(CmdParser::XBodyIdentifierContext *ctx) override {
            GetCurrentSubst().Body = {{Values.InsertVar(ctx->getText())}};
            return visitChildren(ctx);
        }

        std::any visitXBodyString(CmdParser::XBodyStringContext *ctx) override {
            GetCurrentSubst().Body = {{Values.InsertStr(UnquoteDouble(ctx->getText()))}};
            return visitChildren(ctx);
        }

    private:

        std::any doVisitTermR(antlr4::RuleContext *ctx) {
            auto& arg = GetCurrentArgument();
            if (MacroCallDepth == 0)
                arg.emplace_back(Values.InsertStr(Unescape(ctx->getText())));
            else
                arg.emplace_back(TSyntax::TIdOrString{Unescape(ctx->getText())});
            return visitChildren(ctx);
        }

        std::any doVisitTermC(CmdParser::TermCContext *ctx) {
            auto rawArgs = TSyntax::TCommand();
            auto result = std::any();
            {
                CmdStack.push_back(&rawArgs);
                Y_DEFER {CmdStack.pop_back();};
                ++MacroCallDepth;
                Y_DEFER {--MacroCallDepth;};
                result = visitChildren(ctx);
            }
            auto text = ctx->TEXT_VAR()->getText();
            auto macroName = Unvariable(text);
            GetCurrentArgument().emplace_back(TSyntax::TCall{
                .Function = Values.InsertVar(macroName),
                .Arguments = CollectArgs(macroName, std::move(rawArgs))
            });
            return result;
        }

        std::any doVisitTermV(antlr4::RuleContext *ctx) {
            GetCurrentArgument().emplace_back(Values.InsertVar(Unvariable(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any doVisitTermX(antlr4::RuleContext *ctx) {
            GetCurrentArgument().emplace_back(TSyntax::TSubstitution{});
            return visitChildren(ctx);
        }

    private:

        TVector<TSyntax> CollectArgs(TStringBuf macroName, TSyntax::TCommand rawArgs) {
            //
            // block data representations:
            // `macro FOOBAR(A, B[], C...) {...}`  -->  ArgNames == {"B...", "A", "C..."}, Keywords = {"B"}
            //
            // cf. ConvertArgsToPositionalArrays() and MapMacroVars()
            //
            // the resulting collection follows the ordering in ArgNames
            //

            Y_ASSERT(Conf);
            auto blockDataIt = Conf->BlockData.find(macroName);
            auto blockData = blockDataIt != Conf->BlockData.end() ? &blockDataIt->second : nullptr;
            Y_ASSERT(blockData); // TODO handle unknown macros

            auto args = TVector<TSyntax>(blockData->CmdProps->ArgNames.size(), {{TSyntax::TCommand()}});
            TSyntax* namedArg = nullptr;

            auto kwArgCnt = blockData->CmdProps->Keywords.size();
            auto posArgCnt = blockData->CmdProps->ArgNames.size() - blockData->CmdProps->Keywords.size();
            auto hasVarArg = blockData->CmdProps->ArgNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX);
            for (auto rawArg = rawArgs.begin(); rawArg != rawArgs.end(); ++rawArg) {

                if (rawArg->size() == 1)
                    if (auto kw = std::get_if<TSyntax::TIdOrString>(&rawArg->front()))
                        if (blockData->CmdProps->HasKeyword(kw->Value)) {
                            namedArg = &args[blockData->CmdProps->Key2ArrayIndex(kw->Value)];
                            continue;
                        }

                for (auto& term : *rawArg)
                    if (auto str = std::get_if<TSyntax::TIdOrString>(&term))
                        term = Values.InsertStr(str->Value);

                if (namedArg) {
                    namedArg->Commands.back().push_back(std::move(*rawArg));
                    continue;
                }

                size_t rawPos = rawArg - rawArgs.begin();
                if (rawPos < posArgCnt)
                    args[kwArgCnt + rawPos].Commands.back().push_back(std::move(*rawArg));
                else if (hasVarArg)
                    args[kwArgCnt + posArgCnt - 1].Commands.back().push_back(std::move(*rawArg));
                else
                    throw TError()
                        << "Macro " << macroName
                        << " called with too many positional arguments"
                        << " (" << posArgCnt << " expected)";

            }

            return args;
        }

    private:

        TSyntax::TCommand& GetCurrentCommand() {
            return *CmdStack.back();
        }

        TSyntax::TArgument& GetCurrentArgument() {
            return CmdStack.back()->back();
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

        const TBuildConfiguration* Conf;
        TMacroValues& Values;
        TSyntax Syntax;
        TVector<TSyntax::TCommand*> CmdStack;
        int MacroCallDepth = 0;

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

TSyntax NCommands::Parse(const TBuildConfiguration* conf, TMacroValues& values, TStringBuf src) {

    antlr4::ANTLRInputStream input(src);
    TCmdParserErrorListener errorListener(src);

    CmdLexer lexer(&input);
    lexer.addErrorListener(&errorListener);

    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();

    CmdParser parser(&tokens);
    parser.addErrorListener(&errorListener);

    TCmdParserVisitor_Polexpr visitor(conf, values);
    visitor.visit(parser.main());

    return visitor.Extract();

}

namespace {

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
                            auto func = m.Name;
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
                        auto justOneThing
                            = s.Body.size() == 1 && s.Body.front().size() == 1
                            ? &s.Body.front().front()
                            : nullptr;
                        // these special cases are here mostly to reinforce the notion
                        // that `${VAR}` should be equivalent to `$VAR`;
                        // we use it with `${mods:VAR}`, as well,
                        // to cut down on `Args(Terms(...))` wrappers
                        // (the "const" version may appear as a result of preevaluation);
                        // conceptually, with proper typing support,
                        // this should not be required
                        if (auto* id = std::get_if<NPolexpr::TConstId>(justOneThing); id) {
                            termsBuilder.Append(*id);
                        } else if (auto* id = std::get_if<NPolexpr::EVarId>(justOneThing); id) {
                            termsBuilder.Append(*id);
                        } else {
                            CompileArgs(values, s.Body, termsBuilder);
                        }
                    },
                    [&](const TSyntax::TCall&) {
                        Y_ABORT();
                    },
                    [&](const TSyntax::TIdOrString&) {
                        Y_ABORT();
                    },
                    [&](const TSyntax::TUnexpanded&) {
                        Y_ABORT();
                    },
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
