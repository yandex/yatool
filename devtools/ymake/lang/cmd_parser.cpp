#include "cmd_parser.h"

#include <devtools/ymake/lang/CmdLexer.h>
#include <devtools/ymake/lang/CmdParserBaseVisitor.h>
#include <devtools/ymake/conf.h>

#include <devtools/ymake/polexpr/variadic_builder.h>

#include <util/generic/overloaded.h>
#include <util/generic/scope.h>

using namespace NCommands;

namespace {

    EMacroFunction CompileFnName(TStringBuf key) {
        static const THashMap<TStringBuf, EMacroFunction> names = {
            {"hide",    EMacroFunction::Hide},
            {"clear",   EMacroFunction::Clear},
            {"input",   EMacroFunction::Input},
            {"output",  EMacroFunction::Output},
            {"tool",    EMacroFunction::Tool},
            {"pre",     EMacroFunction::Pre},
            {"suf",     EMacroFunction::Suf},
            {"join",    EMacroFunction::Join},
            {"quo",     EMacroFunction::Quo},
            {"rootrel", EMacroFunction::RootRel},
            {"noext",   EMacroFunction::CutExt},
            {"lastext", EMacroFunction::LastExt},
            {"ext",     EMacroFunction::ExtFilter},
            {"cwd",     EMacroFunction::Cwd},
            {"env",     EMacroFunction::SetEnv},
            {"kv",      EMacroFunction::KeyValue},
            {"context", EMacroFunction::Context},
            {"noauto",  EMacroFunction::NoAutoSrc},
            {"norel",   EMacroFunction::NoRel},
            {"tobindir",EMacroFunction::ResolveToBinDir},
            {"glob",    EMacroFunction::Glob},
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
            Syntax.Script.emplace_back();
            CmdStack.push_back(&Syntax.Script.back());
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

        std::any visitTermSQR(CmdParser::TermSQRContext *ctx) override { return doVisitTermQR(ctx); }
        std::any visitTermSQV(CmdParser::TermSQVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermSQX(CmdParser::TermSQXContext *ctx) override { return doVisitTermX(ctx); }

        // double-quoted terms

        std::any visitTermDQR(CmdParser::TermDQRContext *ctx) override { return doVisitTermQR(ctx); }
        std::any visitTermDQV(CmdParser::TermDQVContext *ctx) override { return doVisitTermV(ctx); }
        std::any visitTermDQX(CmdParser::TermDQXContext *ctx) override { return doVisitTermX(ctx); }

        // transformation pieces

        std::any visitXModKey(CmdParser::XModKeyContext *ctx) override {
            GetCurrentXfm().Mods.push_back({CompileFnName(ctx->getText()), {}});
            return visitChildren(ctx);
        }

        std::any visitXModValue(CmdParser::XModValueContext *ctx) override {
            GetCurrentXfm().Mods.back().Values.emplace_back();
            return visitChildren(ctx);
        }

        std::any visitXModValueT(CmdParser::XModValueTContext *ctx) override {
            GetCurrentXfm().Mods.back().Values.back().push_back(Values.InsertStr(ctx->getText()));
            return visitChildren(ctx);
        }

        std::any visitXModValueV(CmdParser::XModValueVContext *ctx) override {
            GetCurrentXfm().Mods.back().Values.back().push_back(Values.InsertVar(Unvariable(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any visitXModValueE(CmdParser::XModValueEContext *ctx) override {
            std::string text = ctx->getText();
            if (!(text.size() > 3 && text.starts_with("${") && text.ends_with("}")))
                throw yexception() << "bad variable name " << text;
            std::string_view sv(text.begin() + 2, text.end() - 1);
            GetCurrentXfm().Mods.back().Values.back().push_back(Values.InsertVar(sv));
            return visitChildren(ctx);
        }

        std::any visitXBodyIdentifier(CmdParser::XBodyIdentifierContext *ctx) override {
            GetCurrentXfm().Body = {{Values.InsertVar(ctx->getText())}};
            return visitChildren(ctx);
        }

        std::any visitXBodyString(CmdParser::XBodyStringContext *ctx) override {
            GetCurrentXfm().Body = {{Values.InsertStr(UnquoteDouble(ctx->getText()))}};
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

        std::any doVisitTermQR(antlr4::RuleContext *ctx) {
            auto& arg = GetCurrentArgument();
            arg.emplace_back(Values.InsertStr(Unescape(ctx->getText())));
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
            GetCurrentArgument().emplace_back(TSyntax::TTransformation{});
            return visitChildren(ctx);
        }

    private:

        TVector<TSyntax> CollectArgs(TStringBuf macroName, TSyntax::TCommand rawArgs) {
            // see the "functions/arg-passing" test for the sorts of patterns this logic is supposed to handle

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

            auto args = TVector<TSyntax>(blockData->CmdProps->ArgNames.size());
            const TKeyword* kwDesc = nullptr;

            auto kwArgCnt = blockData->CmdProps->Keywords.size();
            auto posArgCnt = blockData->CmdProps->ArgNames.size() - blockData->CmdProps->Keywords.size();
            auto hasVarArg = !blockData->CmdProps->ArgNames.empty() && blockData->CmdProps->ArgNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX);

            for (size_t i = 0; i != posArgCnt; ++i)
                args[kwArgCnt + i].Script.emplace_back();
            if (hasVarArg)
                --posArgCnt;

            auto maybeStartNamedArg = [&]() {
                Y_ASSERT(kwDesc);
                auto namedArg = &args[kwDesc->Pos];
                if (kwDesc->To == 0)
                    return;
                if (namedArg->Script.empty())
                    namedArg->Script.emplace_back();
            };

            auto maybeFinishNamedArg = [&]() {
                if (!kwDesc)
                    return;
                auto namedArg = &args[kwDesc->Pos];
                if (kwDesc->To == 0) {
                    if (namedArg->Script.empty()) // it will not be empty if we repeat the respective arg; TBD do we even want to allow this?
                        AssignPreset(namedArg->Script, kwDesc->OnKwPresent);
                    kwDesc = nullptr;
                    return;
                }
                if (namedArg->Script.back().size() == kwDesc->To)
                    kwDesc = nullptr;
            };

            size_t posArg = 0;
            for (auto rawArg = rawArgs.begin(); rawArg != rawArgs.end(); ++rawArg, maybeFinishNamedArg()) {

                if (rawArg->size() == 1)
                    if (auto kw = std::get_if<TSyntax::TIdOrString>(&rawArg->front())) {
                        auto kwDescIt = blockData->CmdProps->Keywords.find(kw->Value);
                        if (kwDescIt != blockData->CmdProps->Keywords.end()) {
                            kwDesc = &kwDescIt->second;
                            maybeStartNamedArg();
                            continue;
                        }
                    }

                for (auto& term : *rawArg)
                    if (auto str = std::get_if<TSyntax::TIdOrString>(&term))
                        term = Values.InsertStr(str->Value);

                if (kwDesc) {
                    auto namedArg = &args[kwDesc->Pos];
                    namedArg->Script.back().push_back(std::move(*rawArg));
                } else {
                    if (posArg < posArgCnt)
                        args[kwArgCnt + posArg].Script.back().push_back(std::move(*rawArg));
                    else if (hasVarArg)
                        args[kwArgCnt + posArgCnt].Script.back().push_back(std::move(*rawArg));
                    else
                        throw TError()
                            << "Macro " << macroName
                            << " called with too many positional arguments"
                            << " (" << posArgCnt << " expected)";
                    ++posArg;
                }

            }

            if (posArg < posArgCnt)
                throw TError()
                    << "Macro " << macroName
                    << " called with too few positional arguments"
                    << " (" << posArgCnt << (hasVarArg ? " or more" : "") << " expected)";

            for (auto& kw : blockData->CmdProps->Keywords) {
                auto namedArg = &args[kw.second.Pos];
                if (!namedArg->Script.empty()) {
                    Y_ASSERT(namedArg->Script.size() == 1);
                    if (namedArg->Script.back().size() < kw.second.From)
                        throw TError()
                            << "Macro " << macroName
                            << " did not get enough data for the named argument " << kw.first;
                }
                if (namedArg->Script.empty())
                    AssignPreset(namedArg->Script, kw.second.OnKwMissing);
            }

            return args;
        }

        void AssignPreset(TSyntax::TScript& dst, const TVector<TString>& src) {
            Y_ASSERT(dst.empty());
            TSyntax::TArgument arg;
            arg.reserve(src.size());
            for (auto& s : src)
                arg.emplace_back(Values.InsertStr(s));
            dst.push_back({std::move(arg)});
        }

    private:

        TSyntax::TCommand& GetCurrentCommand() {
            return *CmdStack.back();
        }

        TSyntax::TArgument& GetCurrentArgument() {
            return CmdStack.back()->back();
        }

        TSyntax::TTransformation& GetCurrentXfm() {
            return std::get<TSyntax::TTransformation>(GetCurrentCommand().back().back());
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
        NPolexpr::TVariadicCallBuilder argsBuilder(cmdsBuilder, values.Func2Id(EMacroFunction::Args));
        for (size_t arg = 0; arg != cmd.size(); ++arg) {
            NPolexpr::TVariadicCallBuilder termsBuilder(argsBuilder, values.Func2Id(EMacroFunction::Terms));
            for (size_t term = 0; term != cmd[arg].size(); ++term) {
                std::visit(TOverloaded{
                    [&](NPolexpr::TConstId s) {
                        termsBuilder.Append(s);
                    },
                    [&](NPolexpr::EVarId v) {
                        termsBuilder.Append(v);
                    },
                    [&](const TSyntax::TTransformation& x) {
                        for (auto&& m : x.Mods) {
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
                                    NPolexpr::TVariadicCallBuilder catBuilder(termsBuilder, values.Func2Id(EMacroFunction::Cat));
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
                            = x.Body.size() == 1 && x.Body.front().size() == 1
                            ? &x.Body.front().front()
                            : nullptr;
                        // these special cases are here mostly to reinforce the notion
                        // that `${VAR}` should be equivalent to `$VAR`;
                        // we use it with `${mods:VAR}`, as well,
                        // to cut down on `Args(Terms(...))` wrappers
                        // (the "const" version may appear as a result of preevaluation);
                        // conceptually, with proper typing support,
                        // this should not be required
                        if (auto* id = std::get_if<NPolexpr::TConstId>(justOneThing)) {
                            termsBuilder.Append(*id);
                        } else if (auto* id = std::get_if<NPolexpr::EVarId>(justOneThing)) {
                            termsBuilder.Append(*id);
                        } else {
                            CompileArgs(values, x.Body, termsBuilder);
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
    NPolexpr::TVariadicCallBuilder cmdsBuilder(result, values.Func2Id(EMacroFunction::Cmds));
    for (size_t cmd = 0; cmd != s.Script.size(); ++cmd)
        CompileArgs(values, s.Script[cmd], cmdsBuilder);
    cmdsBuilder.Build();
    return result;
}
