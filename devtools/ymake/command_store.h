#pragma once

#include <devtools/ymake/exec.h>
#include <devtools/ymake/polexpr/expression.h>
#include <devtools/ymake/lang/cmd_parser.h>
#include <devtools/ymake/lang/macro_values.h>
#include <devtools/ymake/vars.h>
#include <devtools/ymake/symbols/cmd_store.h>

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/deque.h>

class TAddDepAdaptor;
class TDepGraph;

namespace NCommands {
    struct TEvalCtx;
    class TScriptEvaluator;
}

enum class ECmdId: ui32 {
    Invalid = ~0u
};

enum class EOutputAccountingMode {
    Default, // full enumeration, e.g., in nodes originating from the SRCS macro
    Module // implicit main output
};

class TCommands {
    friend NCommands::TScriptEvaluator;

public:
    struct TCompiledCommand {
        struct TInput {
            TStringBuf Name;
            bool IsGlob = false;
            TInput(TStringBuf name) : Name(name) {}
            operator TStringBuf() const { return Name; }
        };
        struct TOutput {
            TStringBuf Name;
            bool NoAutoSrc = false;
            TOutput(TStringBuf name): Name(name) {}
            operator TStringBuf() const { return Name; }
        };
        template<typename TLink>
        class TLinks:
            public TUniqContainerImpl<TLink, TStringBuf, 32, TVector<TLink>, true> // basically, TUniqVector<TLink> with IsIndexed=true
        {
        public:
            ui32 Base = 0;
        };
        using TInputs = TLinks<TInput>;
        using TOutputs = TLinks<TOutput>;

        NPolexpr::TExpression Expression;
        TInputs Inputs;
        TOutputs Outputs;
    };

    struct SimpleCommandSequenceWriter: TCommandSequenceWriterStubs {
        auto& Write(
            const TCommands& commands,
            const NPolexpr::TExpression& cmdExpr,
            const TVars& vars,
            const TVector<std::span<TVarStr>>& inputs,
            TCommandInfo& cmd,
            const TCmdConf* cmdConf
        ) {
            commands.WriteShellCmd(this, cmdExpr, vars, inputs, cmd, cmdConf);
            return *this;
        }
        auto Extract() {
            return std::exchange(Script, {});
        }
    public: // ICommandSequenceWriter
        void BeginScript() override {
        }
        void BeginCommand() override {
            Script.emplace_back();
        }
        void WriteArgument(TStringBuf arg) override {
            Script.back().push_back(TString(arg));
        }
        void EndCommand() override {
        }
        void EndScript(TCommandInfo&, const TVars&) override {
        }
    private:
        TVector<TVector<TString>> Script;
    };

public:
    const NPolexpr::TExpression* Get(ECmdId id) const {
        const auto uId = static_cast<ui32>(id);
        if (Y_UNLIKELY(Commands.size() <= uId)) {
            return nullptr;
        }
        return &Commands[uId];
    }

    const NPolexpr::TExpression* Get(TStringBuf name, const TCmdConf *conf) const;

    ECmdId IdByElemId(ui32 elemId) const {
        const auto fres = Elem2Cmd.find(elemId);
        if (fres == Elem2Cmd.end()) {
            return ECmdId::Invalid;
        }
        return fres->second;
    }

    const NPolexpr::TExpression* GetByElemId(ui32 elemId) const {
        const auto fres = Elem2Cmd.find(elemId);
        if (fres == Elem2Cmd.end() || fres->second == ECmdId::Invalid) {
            return nullptr;
        }
        return &Commands[static_cast<ui32>(fres->second)];
    }
    TCompiledCommand Compile(TStringBuf cmd, const TVars& inlineVars, const TVars& allVars, bool preevaluate, EOutputAccountingMode oam = EOutputAccountingMode::Default);
    ui32 Add(TDepGraph& graph, NPolexpr::TExpression expr);

    TString PrintCmd(const NPolexpr::TExpression& cmdExpr) const;
    void StreamCmdRepr(const NPolexpr::TExpression& cmdExpr, std::function<void(const char* data, size_t size)> sink) const;

    TCompiledCommand Preevaluate(const NPolexpr::TExpression& expr, const TVars& vars, EOutputAccountingMode oam);

    void WriteShellCmd(
        ICommandSequenceWriter* writer,
        const NPolexpr::TExpression& cmdExpr,
        const TVars& vars,
        const TVector<std::span<TVarStr>>& inputs,
        TCommandInfo& cmd,
        const TCmdConf* cmdConf
    ) const;

    // TODO collect vars and tools while compiling
    TVector<TStringBuf> GetCommandVars(ui32 elemId) const;
    TVector<TStringBuf> GetCommandTools(ui32 elemId) const;

    void Save(TMultiBlobBuilder& builder) const;
    void Load(const TBlob& multi);

    template<typename F>
    void ForEachCommand(F f) const {
        for (size_t i = 0; i != Commands.size(); ++i)
            f(static_cast<ECmdId>(i), Commands[i]);
    }

protected:
    TMacroValues& GetValues() {
        return Values;
    }

private:
    TString ConstToString(const TMacroValues::TValue& value, const NCommands::TEvalCtx& ctx) const;
    TVector<TString> InputToStringArray(const TMacroValues::TInput& input, const NCommands::TEvalCtx& ctx) const;
    TString PrintRawCmdNode(NPolexpr::TConstId node) const;
    TString PrintRawCmdNode(NPolexpr::EVarId node) const;

    using TMinedVars = THashMap<
        TStringBuf, // name
        TVector<THolder<NCommands::TSyntax>> // definitions indexed by recursion depth
    >;
    struct TCmdWriter;
    const NCommands::TSyntax& Parse(TMacroValues& values, TString src);
    void Premine(const NCommands::TSyntax& ast, const TVars& inlineVars, const TVars& allVars, TMinedVars& newVars);
    void InlineModValueTerm(const NCommands::TSyntax::TSubstitution::TModifier::TValueTerm& term, const TMinedVars& vars, NCommands::TSyntax::TSubstitution::TModifier::TValue& writer);
    void InlineScalarTerms(const NCommands::TSyntax::TArgument& arg, const TMinedVars& vars, TCmdWriter& writer);
    void InlineArguments(const NCommands::TSyntax::TCommand& cmd, const TMinedVars& vars, TCmdWriter& writer);
    void InlineCommands(const NCommands::TSyntax::TCommands& cmds, const TMinedVars& vars, TCmdWriter& writer);
    NCommands::TSyntax Inline(const NCommands::TSyntax& ast, const TMinedVars& vars);

    TDeque<NPolexpr::TExpression> Commands;
    THashMap<ui64, ECmdId> Command2Id;
    THashMap<ui32, ECmdId> Elem2Cmd;
    TMacroValues Values;

    THashMap<TString, NCommands::TSyntax> ParserCache;
    THashMap<TStringBuf, size_t> VarRecursionDepth;
};
