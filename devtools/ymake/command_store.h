#pragma once

#include <devtools/ymake/exec.h>
#include <devtools/ymake/polexpr/expression.h>
#include <devtools/ymake/lang/cmd_parser.h>
#include <devtools/ymake/lang/macro_values.h>
#include <devtools/ymake/vars.h>
#include <devtools/ymake/symbols/cmd_store.h>
#include <devtools/ymake/options/debug_options.h>
#include <devtools/ymake/commands/mod_registry.h>

#include <util/generic/hash.h>
#include <util/generic/strbuf.h>
#include <util/generic/deque.h>

class TBuildConfiguration;
class TAddDepAdaptor;
class TDepGraph;

namespace NCommands {
    struct TEvalCtx;
    class TScriptEvaluator;
}

enum class ECmdId: ui32 {
    Invalid = ~0u
};

struct TCompilationIODesc {

    // non-empty for modules, empty for macros in general;
    // if present, this will be registered as the 0th output;
    // explicit module-name outputs in commands should end up referencing this one
    std::string_view MainOutput;

    TSpecFileList* KnownInputs = nullptr;
    TSpecFileList* KnownOutputs = nullptr;

};

struct TErrorShowerState {
    using EShowExpressionErrors = TDebugOptions::EShowExpressionErrors;
    EShowExpressionErrors Mode;
    size_t Depth = size_t(-1);
    size_t Count = 0;
    explicit TErrorShowerState(EShowExpressionErrors mode):
        Mode(mode)
    {
    }
    bool Accept(size_t curDepth) {
        switch (Mode) {
            case EShowExpressionErrors::None:
                return false;
            case EShowExpressionErrors::All:
                return true;
            case EShowExpressionErrors::One: {
                auto result = Depth > curDepth;
                if (result)
                    Depth = curDepth;
                return result;
            }
        }
    }
};

struct TToolsAndResults {
    TVector<TStringBuf> Tools;
    TVector<TStringBuf> Results;
};

class TCommands {
    friend NCommands::TScriptEvaluator;

public:
    struct SimpleCommandSequenceWriter: TCommandSequenceWriterStubs {
        auto& Write(
            const TCommands& commands,
            const NPolexpr::TExpression& cmdExpr,
            const TVars& vars,
            const TVector<std::span<TVarStr>>& inputs,
            TCommandInfo& cmd,
            const TCmdConf* cmdConf,
            const TBuildConfiguration& buildConf
        ) {
            auto ignoreErrors = TErrorShowerState(TDebugOptions::EShowExpressionErrors::None);
            commands.WriteShellCmd(this, cmdExpr, vars, inputs, cmd, cmdConf, buildConf, &ignoreErrors);
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
            Script.back().emplace_back(arg);
        }
        void EndCommand() override {
        }
        void EndScript(TCommandInfo&, const TVars&) override {
        }
    private:
        TVector<TVector<TString>> Script;
    };

public:
    ui32 EngineTag() const {
        return 0;
    }

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
    NCommands::TCompiledCommand Compile(
        TStringBuf cmd,
        const TBuildConfiguration& conf,
        const TVars& vars,
        bool preevaluate,
        TCompilationIODesc io
    );
    ui32 Add(TDepGraph& graph, NPolexpr::TExpression expr);

    TString PrintExpr(const NCommands::TSyntax& expr) const;
    TString PrintCmd(const NPolexpr::TExpression& cmdExpr, size_t highlightBegin = -1, size_t highlightEnd = -1) const;
    void StreamCmdRepr(const NPolexpr::TExpression& cmdExpr, std::function<void(const char* data, size_t size)> sink) const;

    NCommands::TCompiledCommand Preevaluate(NCommands::TSyntax& expr, const TVars& vars, TCompilationIODesc io);

    void WriteShellCmd(
        ICommandSequenceWriter* writer,
        const NPolexpr::TExpression& cmdExpr,
        const TVars& vars,
        const TVector<std::span<TVarStr>>& inputs,
        TCommandInfo& cmd,
        const TCmdConf* cmdConf,
        const TBuildConfiguration& buildConf,
        TErrorShowerState* errorShower
    ) const;

    // TODO collect vars and tools while compiling
    TVector<TStringBuf> GetCommandVars(ui32 elemId) const;
    TToolsAndResults GetCommandToolsEtc(ui32 elemId) const;

    void Save(TMultiBlobBuilder& builder) const;
    void Load(const TBlob& multi);

    template<typename F>
    void ForEachCommand(F f) const {
        for (size_t i = 0; i != Commands.size(); ++i)
            f(static_cast<ECmdId>(i), Commands[i]);
    }

protected:
    auto& GetValues() {
        return Values;
    }

    auto& GetMods() {
        return Mods;
    }

private:
    NCommands::TTermValue EvalConst(const TMacroValues::TValue& value, const NCommands::TEvalCtx& ctx) const;
    TVector<TString> InputToStringArray(const TMacroValues::TInput& input, const NCommands::TEvalCtx& ctx) const;
    TString PrintRawCmdNode(NPolexpr::TConstId node) const;
    TString PrintRawCmdNode(NPolexpr::EVarId node) const;
    void PrintCmd(const NCommands::TSyntax::TCommand& cmd, IOutputStream& os) const;
    TString PrintConst(NPolexpr::TConstId id) const;

    const NCommands::TSyntax& Parse(const TBuildConfiguration& conf, const NCommands::TModRegistry& mods, TMacroValues& values, TString src);

    struct TInlinerSideChannels {
        // TODO merge inlining with preevaluation, dump this stuff directly into TCompiledCommand
        THolder<TVector<TStringBuf>> AddIncls;
        THolder<TVector<TStringBuf>> AddPeers;
    };

    struct TCmdWriter;
    struct TInliner {
        TInliner(
            const TBuildConfiguration& conf,
            TCommands& commands,
            const TVars& vars
        ):
            Conf(conf),
            Commands(commands),
            LegacyVars{
                .Vars = vars
            }
        {}
    public:
        std::pair<NCommands::TSyntax, TInlinerSideChannels> Inline(const NCommands::TSyntax& ast);
    private:
        enum class ELegacyMode {None, Expr, Macro};
        struct TVarDefinition {
            const NCommands::TSyntax* Definition = nullptr;
            ELegacyMode LegacyMode = ELegacyMode::None;
        };
        struct TScope;
        TVarDefinition GetVariableDefinition(NPolexpr::EVarId id);
        const NCommands::TSyntax* GetMacroDefinition(NPolexpr::EVarId id);
        void ProcessMacroSideChannels(const NCommands::TSyntax::TCall& src);
        void FillMacroArgs(const NCommands::TSyntax::TCall& src, TScope& dst);
        void InlineModArgTerm(const NCommands::TSyntax::TTerm& term, NCommands::TSyntax::TArgument& writer);
        void InlineTransformation(const NCommands::TSyntax::TTransformation& xfm, NCommands::TSyntax::TTransformation& writer);
        void InlineScalarTerm(const NCommands::TSyntax::TTerm& term, TCmdWriter& writer);
        void InlineScalarTerms(const NCommands::TSyntax::TArgument& arg, TCmdWriter& writer);
        void InlineArguments(const NCommands::TSyntax::TCommand& cmd, TCmdWriter& writer);
        void InlineCommands(const NCommands::TSyntax::TScript& scr, TCmdWriter& writer);
    private: // context
        const TBuildConfiguration& Conf;
        TCommands& Commands;
    private: // variable layers (note that macros are variables, too)
        const NCommands::TSyntax* VarLookup(TStringBuf name);
        const TScope* Scope = nullptr;
        struct TLegacyVars {
            struct TDefinition {THolder<NCommands::TSyntax> Definition; ELegacyMode LegacyMode;};
            using TDefinitions = TVector<TDefinition>; // indexed by recursion depth
            using TDefinitionCache = THashMap<TStringBuf, THolder<TDefinitions>>;
            using TRecursionDepth = THashMap<NPolexpr::EVarId, size_t>;
            const TVars& Vars;
            TDefinitionCache DefinitionCache = {};
            TRecursionDepth RecursionDepth = {};
            const TYVar* VarLookup(TStringBuf name, const TBuildConfiguration& conf);
        } LegacyVars;
    private: // misc
        int Depth = 0;
        void CheckDepth();
    private:
        TInlinerSideChannels SideChannels;
    };

private:
    TDeque<NPolexpr::TExpression> Commands;
    THashMap<ui64, ECmdId> Command2Id;
    THashMap<ui32, ECmdId> Elem2Cmd;
    TMacroValues Values;
    const NCommands::TModRegistry Mods;

    THashMap<TString, NCommands::TSyntax> ParserCache;
};
