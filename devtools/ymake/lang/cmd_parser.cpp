#include "cmd_parser.h"

#include <devtools/ymake/lang/CmdLexer.h>
#include <devtools/ymake/lang/CmdParserBaseVisitor.h>
#include <devtools/ymake/commands/mod_registry.h>
#include <devtools/ymake/conf.h>

#include <devtools/ymake/polexpr/variadic_builder.h>

#include <util/generic/overloaded.h>
#include <util/generic/scope.h>

using namespace NCommands;

namespace {

    class TCmdParserVisitor_Polexpr: public CmdParserBaseVisitor {

    public:

        TCmdParserVisitor_Polexpr(const TBuildConfiguration* conf, const TModRegistry& mods, TMacroValues& values)
            : Conf(conf)
            , Mods(mods)
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

        std::any visitTermsSQ(CmdParser::TermsSQContext *ctx) override { return doVisitTermsQ(ctx); }
        std::any visitTermsDQ(CmdParser::TermsDQContext *ctx) override { return doVisitTermsQ(ctx); }

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
            auto name = ctx->getText();
            auto id = Mods.TryGetId(name);
            if (Y_UNLIKELY(!id))
                throw yexception() << "unknown modifier " << name;
            GetCurrentXfm().Mods.push_back({*id, {}});
            return visitChildren(ctx);
        }

        std::any visitXModValue(CmdParser::XModValueContext *ctx) override {
            GetCurrentXfm().Mods.back().Arguments.emplace_back();
            return visitChildren(ctx);
        }

        std::any visitXModValueT(CmdParser::XModValueTContext *ctx) override {
            GetCurrentXfm().Mods.back().Arguments.back().push_back(Values.InsertStr(UnescapeRaw(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any visitXModValueV(CmdParser::XModValueVContext *ctx) override {
            GetCurrentXfm().Mods.back().Arguments.back().push_back(Values.InsertVar(Unvariable(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any visitXModValueE(CmdParser::XModValueEContext *ctx) override {
            std::string text = ctx->getText();
            if (!(text.size() > 3 && text.starts_with("${") && text.ends_with("}")))
                throw yexception() << "bad variable name " << text;
            std::string_view sv(text.begin() + 2, text.end() - 1);
            GetCurrentXfm().Mods.back().Arguments.back().push_back(Values.InsertVar(sv));
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

        std::any doVisitTermsQ(antlr4::RuleContext *ctx) {
            auto result = visitChildren(ctx);
            auto& arg = GetCurrentArgument();

            // make sure that:
            // * enquoted strings stay as command arguments even when they are empty;
            // * enquoted strings are treated as if being at the term level
            //   even when they haven't been concatenated with anything;
            // note that the current argument can have other terms
            // before and after the string being parsed,
            // but that should not cause any issues
            bool hack_the_system = false;
            if (arg.size() == 0) {
                hack_the_system = true;
            } else if (arg.size() == 1) {
                auto val = std::get_if<NPolexpr::TConstId>(&arg[0]);
                hack_the_system = !(val && val->GetStorage() == TMacroValues::ST_LITERALS);
            }
            if (hack_the_system)
                arg.emplace_back(Values.InsertStr(""));

            return result;
        }

        std::any doVisitTermR(antlr4::RuleContext *ctx) {
            auto& arg = GetCurrentArgument();
            if (MacroCallDepth == 0)
                arg.emplace_back(Values.InsertStr(UnescapeRaw(ctx->getText())));
            else
                arg.emplace_back(TSyntax::TIdOrString{UnescapeRaw(ctx->getText())});
            return visitChildren(ctx);
        }

        std::any doVisitTermQR(antlr4::RuleContext *ctx) {
            auto& arg = GetCurrentArgument();
            arg.emplace_back(Values.InsertStr(UnescapeQuoted(ctx->getText())));
            return visitChildren(ctx);
        }

        std::any doVisitTermC(CmdParser::TermCContext *ctx) {
            static const auto ifBlockData = [&]() {
                // cf. TConfBuilder::EnterMacro
                auto keywords = TCmdProperty::TKeywords();
                keywords.AddKeyword("THEN", 0, -1, "");
                keywords.AddKeyword("ELSE", 0, -1, "");
                auto blockData = TBlockData{};
                blockData.Completed = true;
                blockData.IsUserMacro = true;
                const TVector<TString> positionalArgs = {"COND"};
                blockData.CmdProps.Reset(new TCmdProperty{positionalArgs, std::move(keywords)});
                return blockData;
            }();

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
            if (macroName == "IF") {
                auto args = CollectArgs(macroName, ifBlockData, std::move(rawArgs));
                auto& cond = FindCallArg(args, "COND");
                auto& then = FindCallArg(args, "THEN");
                auto& notThen = FindCallArg(args, "ELSE");
                if (!(cond.Script.size() == 1 && cond.Script.front().size() == 1 && cond.Script.front().front().size() == 1)) [[unlikely]]
                    throw TError() << "Bad condition shape";
                if (then.Script.size() != 1) [[unlikely]]
                    throw TError() << "Bad then-branch shape";
                if (notThen.Script.size() != 1) [[unlikely]]
                    throw TError() << "Bad else-branch shape";
                GetCurrentArgument().emplace_back(TSyntax::TBuiltinIf{
                    .Cond = MakeSimpleShared<TSyntax::TTerm>(cond.Script.front().front().front()),
                    .Then = std::move(then.Script.front()),
                    .Else = std::move(notThen.Script.front())
                });
            } else {
                Y_ASSERT(Conf);
                auto blockDataIt = Conf->BlockData.find(macroName);
                if (blockDataIt == Conf->BlockData.end()) [[unlikely]]
                    throw TError() << "Macro " << macroName << " not found";
                GetCurrentArgument().emplace_back(CollectArgs(macroName, blockDataIt->second, std::move(rawArgs)));
            }
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

        const TSyntax& FindCallArg(const TSyntax::TCall& call, TStringBuf name) {
            auto it = std::find(call.ArgumentNames.begin(), call.ArgumentNames.end(), name);
            if (it == call.ArgumentNames.end()) [[unlikely]]
                ythrow TError() << "call argument " << name << " expected";
            return call.Arguments[it - call.ArgumentNames.begin()];
        }

        TSyntax::TCall CollectArgs(TStringBuf macroName, const TBlockData& blockData, TSyntax::TCommand rawArgs) {
            // see the "functions/arg-passing" test for the sorts of patterns this logic is supposed to handle

            //
            // block data representations:
            // `macro FOOBAR(A, B[], C...) {...}`  -->  ArgNames == {"B...", "A", "C..."}, Keywords = {"B"}
            //
            // cf. ConvertArgsToPositionalArrays() and MapMacroVars()
            //
            // the resulting collection follows the ordering in ArgNames
            //

            auto args = TVector<TSyntax>(blockData.CmdProps->ArgNames().size());
            const TKeyword* kwDesc = nullptr;

            auto kwArgCnt = blockData.CmdProps->GetKeyArgsNum();
            auto posArgCnt = blockData.CmdProps->ArgNames().size() - blockData.CmdProps->GetKeyArgsNum();
            auto hasVarArg = posArgCnt != 0 && blockData.CmdProps->ArgNames().back().EndsWith(NStaticConf::ARRAY_SUFFIX);

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
                        if (auto *keywordData = blockData.CmdProps->GetKeywordData(kw->Value)) {
                            kwDesc = keywordData;
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

            for (const auto& [name, kw] : blockData.CmdProps->GetKeywords()) {
                auto namedArg = &args[kw.Pos];
                if (!namedArg->Script.empty()) {
                    Y_ASSERT(namedArg->Script.size() == 1);
                    if (namedArg->Script.back().size() < kw.From)
                        throw TError()
                            << "Macro " << macroName
                            << " did not get enough data for the named argument " << name;
                }
                if (namedArg->Script.empty())
                    AssignPreset(namedArg->Script, kw.OnKwMissing);
            }

            auto argNames = std::vector<TStringBuf>();
            argNames.reserve(blockData.CmdProps->ArgNames().size());
            for (auto& name : blockData.CmdProps->ArgNames()) {
                argNames.push_back(name);
                if (argNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX))
                    argNames.back().Chop(strlen(NStaticConf::ARRAY_SUFFIX));
            }

            Y_ASSERT(args.size() == argNames.size());
            return TSyntax::TCall{
                .Function = Values.InsertVar(macroName),
                .Arguments = std::move(args),
                .ArgumentNames = std::move(argNames)
            };
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
            return UnescapeQuoted(s.substr(1, s.size() - 2));
        }

        std::string UnescapeQuoted(std::string_view s) {
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

        std::string UnescapeRaw(std::string_view s) {
            std::string result;
            result.reserve(s.size());
            for (size_t i = 0; i != s.size(); ++i) {
                if (s[i] == '\\')
                    if (++i == s.size())
                        throw yexception() << "incomplete escape sequence in " << s;
                result += s[i];
            }
            return result;
        }

    private:

        const TBuildConfiguration* Conf;
        const TModRegistry& Mods;
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
            throw yexception() << "could not parse command (" << line << ":" << charPositionInLine << "): " << msg << "\n" << Cmd;
        }
    private:
        TStringBuf Cmd;
    };

}

//
//
//

TSyntax NCommands::Parse(const TBuildConfiguration* conf, const TModRegistry& mods, TMacroValues& values, TStringBuf src) {

    antlr4::ANTLRInputStream input(src);
    TCmdParserErrorListener errorListener(src);

    CmdLexer lexer(&input);
    lexer.removeErrorListeners();
    lexer.addErrorListener(&errorListener);

    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();

    CmdParser parser(&tokens);
    parser.removeErrorListeners();
    parser.addErrorListener(&errorListener);

    TCmdParserVisitor_Polexpr visitor(conf, mods, values);
    visitor.visit(parser.main());

    return visitor.Extract();

}

namespace {

    void CompileTerm(const TModRegistry& mods, const TSyntax::TTerm& term, NPolexpr::TVariadicCallBuilder& termsBuilder);
    void CompileArgs(const TModRegistry& mods, const TSyntax::TCommand& cmd, NPolexpr::TVariadicCallBuilder& cmdsBuilder);

    void CompileTransformation(
        const TModRegistry& mods,
        const TSyntax::TTransformation& x,
        NPolexpr::TVariadicCallBuilder& builder,
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
                CompileTerm(mods, x.Body.front().front(), builder);
            else
                CompileArgs(mods, x.Body, builder);
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
                    CompileTerm(mods, v[0], builder);
                else {
                    NPolexpr::TVariadicCallBuilder catBuilder(builder, mods.Func2Id(EMacroFunction::Cat));
                    for (auto& t : v)
                        CompileTerm(mods, t, catBuilder);
                    catBuilder.Build<EMacroFunction>();
                }
            }
            CompileTransformation(mods, x, builder, pos + 1);
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

    void CompileTerm(const TModRegistry& mods, const TSyntax::TTerm& term, NPolexpr::TVariadicCallBuilder& termsBuilder) {
        std::visit(TOverloaded{
            [&](NPolexpr::TConstId s) {
                termsBuilder.Append(s);
            },
            [&](NPolexpr::EVarId v) {
                termsBuilder.Append(v);
            },
            [&](const TSyntax::TTransformation& x) {
                CompileTransformation(mods, x, termsBuilder);
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

    void CompileArgs(const TModRegistry& mods, const TSyntax::TCommand& cmd, NPolexpr::TVariadicCallBuilder& cmdsBuilder) {
        NPolexpr::TVariadicCallBuilder argsBuilder(cmdsBuilder, mods.Func2Id(EMacroFunction::Args));
        for (auto& arg : cmd) {
            NPolexpr::TVariadicCallBuilder termsBuilder(argsBuilder, mods.Func2Id(EMacroFunction::Terms));
            for (auto& term : arg)
                CompileTerm(mods, term, termsBuilder);
            termsBuilder.Build<EMacroFunction>();
        }
        argsBuilder.Build<EMacroFunction>();
    }

}

NPolexpr::TExpression NCommands::Compile(const TModRegistry& mods, const TSyntax& s) {
    NPolexpr::TExpression result;
    NPolexpr::TVariadicCallBuilder cmdsBuilder(result, mods.Func2Id(EMacroFunction::Cmds));
    for (size_t cmd = 0; cmd != s.Script.size(); ++cmd)
        CompileArgs(mods, s.Script[cmd], cmdsBuilder);
    cmdsBuilder.Build<EMacroFunction>();
    return result;
}
