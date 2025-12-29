#pragma once

#include "macro.h"

#include <devtools/ymake/dirs.h>
#include <devtools/ymake/macro_vars.h>
#include <devtools/ymake/commands/compilation.h>

#include <util/generic/maybe.h>
#include <util/generic/hash.h>
#include <util/generic/string.h>
#include <util/generic/variant.h>
#include <util/generic/vector.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

#include <utility>
#include <span>

class TYMake;
class TModule;
class TModuleBuilder;
class TBuildConfiguration;
class TDepGraph;
class TUpdIter;
class TAddDepAdaptor;
class TCommands;
struct TNodeAddCtx;

bool IsInternalReservedVar(const TStringBuf& cur);

enum ESubstMode {
    ESM_DoSubst = 0,
    ESM_DoFillCoord = 1,
    ESM_DoBoth = 2,
    ESM_DoBothCm = 3,
};

struct TCommandInfo {
    enum ECmdInfoState {
        OK = 0,
        FAILED = 1,
        SKIPPED = 2,
    };

    using TSubstObserver = std::function<void(const TVarStr&)>;

    explicit TCommandInfo(const TBuildConfiguration& conf, TDepGraph* graph, TUpdIter* updIter, TModule* module = nullptr);
    void SetCommandSink(TCommands* commands);
    void SetCommandSource(const TCommands* commands);
    bool Init(const TStringBuf& sname, TVarStrEx& src, const TVector<TStringBuf>* args, TModuleBuilder& mod, TVars* extraVars = nullptr);

public:
    TYVar Cmd; // dep for the main output
    THolder<TVars> ExtraVars;
    THolder<TVars> LocalVars;
    THolder<TVars> GlobalVars; // TODO remove this when TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes is out
    THolder<THashMap<TString, TString>> KV;
    THolder<THashMap<TString, TString>> ToolPaths;
    THolder<THashMap<TString, TString>> ResultPaths;
    THolder<TVector<TString>> LateOuts;
    struct TMultiCmdDescr* MkCmdAcceptor = nullptr; // for command builder only
    bool KeepTargetPlatform = false;
    bool DisableStructCmd = false;

private:
    friend class TCmdProperty;
    explicit TCommandInfo();
    struct TSpecFileLists {
        TSpecFileList Input;         // deps for the main output
        TSpecFileList AutoInput;     // inputs added automatically by ymake
        TSpecFileList Output;        // deps for the main output
        TSpecFileList OutputInclude; // includes for the main output
        TSpecFileList Tools;         // deps for Cmd
        TSpecFileList Results;       // deps for Cmd
        THashMap<TString, TSpecFileList> OutputIncludeForType;
    };

    struct TSpecFileArrs {
        TSpecFileArr Input;
        TSpecFileArr AutoInput;
        TSpecFileArr Output;
        TSpecFileArr OutputInclude;
        TSpecFileArr Tools;
        TSpecFileArr Results;
        THashMap<TString, TSpecFileArr> OutputIncludeForType;
    };

    TFileView InputDir;
    TString InputDirStr; // dir part for Input[0]
    TFileView BuildDir;
    TString BuildDirStr;

    const TBuildConfiguration* Conf;
    TDepGraph* Graph;
    TCommands* CommandSink = nullptr; // intended for the new command engine, stores compilation results
    const TCommands* CommandSource = nullptr; // intended for the old command engine, provides contents of global vars coming in from new-style modules
    TUpdIter* UpdIter;

    std::variant<TSpecFileLists, TSpecFileArrs> SpecFiles;
    THolder<THashMap<TString, TString>> Requirements;

    TModule* Module;
    TVarStrEx* MainInput = nullptr;
    TVarStrEx* MainOutput = nullptr;
    ui32 MainInputCandidateIdx = Max<ui32>();

    mutable ui8 MsgDepth = 0; // for debug messages
    bool AllVarsNeedSubst = false;
    bool HasGlobalInput = false;

    THolder<TVector<TStringBuf>> AddIncls;
    THolder<TVector<TStringBuf>> AddPeers;

    TSpecFileList& GetInputInternal() { return std::get<0>(SpecFiles).Input; }
    TSpecFileList& GetAutoInputInternal() { return std::get<0>(SpecFiles).AutoInput; }
    TSpecFileList& GetOutputInternal() { return std::get<0>(SpecFiles).Output; }
    TSpecFileList& GetOutputIncludeInternal() { return std::get<0>(SpecFiles).OutputInclude; }
    TSpecFileList& GetToolsInternal() { return std::get<0>(SpecFiles).Tools; }
    TSpecFileList& GetResultsInternal() { return std::get<0>(SpecFiles).Results; }

    TSpecFileList& GetOutputIncludeForTypeInternal(TStringBuf type) {
        return std::get<0>(SpecFiles).OutputIncludeForType[type];
    }

public:
    // The only public user of this one is mkcmd.cpp :(
    void AddOutputInternal(const TVarStrEx& out) {
        GetOutputInternal().Push(out);
    }

    std::span<const TVarStrEx> GetInput() const { return SpecFiles.index() == 1 ? std::get<1>(SpecFiles).Input : std::get<0>(SpecFiles).Input.Data(); }
    std::span<const TVarStrEx> GetAutoInput() const { return SpecFiles.index() == 1 ? std::get<1>(SpecFiles).AutoInput : std::get<0>(SpecFiles).AutoInput.Data(); }
    std::span<const TVarStrEx> GetOutput() const { return SpecFiles.index() == 1 ? std::get<1>(SpecFiles).Output : std::get<0>(SpecFiles).Output.Data(); }
    std::span<const TVarStrEx> GetOutputInclude() const { return SpecFiles.index() == 1 ? std::get<1>(SpecFiles).OutputInclude : std::get<0>(SpecFiles).OutputInclude.Data(); }
    std::span<const TVarStrEx> GetTools() const { return SpecFiles.index() == 1 ? std::get<1>(SpecFiles).Tools : std::get<0>(SpecFiles).Tools.Data(); }
    std::span<const TVarStrEx> GetResults() const { return SpecFiles.index() == 1 ? std::get<1>(SpecFiles).Results : std::get<0>(SpecFiles).Results.Data(); }

    std::span<TVarStrEx> GetInput() { return std::get<1>(SpecFiles).Input; }
    std::span<TVarStrEx> GetOutput() { return std::get<1>(SpecFiles).Output; }

    THashMap<TString, TString> TakeRequirements();

    void InitFromModule(const TModule& mod);

    ECmdInfoState CheckInputs(TModuleBuilder& mod, TAddDepAdaptor& node, bool lastTry);
    const TVarStrEx* GetMainOutput() const {
        return MainOutput;
    }
    bool Process(TModuleBuilder& mod, TAddDepAdaptor& node, bool finalTargetCmd);
    bool ProcessVar(TModuleBuilder& mod, TAddDepAdaptor& node);
    void AddCfgVars(const TVector<TDepsCacheId>& varLists, TNodeAddCtx& dst);

    bool GetCommandInfoFromStructCmd(
        TCommands& commands,
        ui32 cmdElemId,
        NCommands::TCompiledCommand& compiled,
        bool skipMainOutput,
        const TVars& vars
    );
    bool GetCommandInfoFromStructVar(
        ui32 varElemId,
        ui32 cmdElemId,
        TCommands& commands,
        const TVars& vars
    );
    bool GetCommandInfoFromMacro(const TStringBuf& macroName, EMacroType type, const TVector<TStringBuf>& args, const TVars& vars, ui64 id);
    TString SubstMacro(const TYVar* origin, TStringBuf pattern, ESubstMode substMode, const TVars& subst, ECmdFormat cmdFormat, bool patHasPrefix, ECmdFormat formatFor = ECF_Unset);

    TString SubstMacroDeeply(const TYVar* origin, const TStringBuf& macro, const TVars& vars, bool patternHasPrefix = false, ECmdFormat cmdFormat = ECF_ExpandVars);
    TString SubstVarDeeply(const TStringBuf& varName, const TVars& vars, ECmdFormat cmdFormat = ECF_ExpandVars);

    // Convenience overload performing vars.Lookop(macroData.Name) with necessary fallbacks and calls full SubstData implementation
    bool SubstData(
        const TYVar* origin,
        TMacroData& macro,
        const TVars& vars,
        ECmdFormat cmdFormat,
        ESubstMode substMode,
        TString& result,
        ECmdFormat formatFor = ECF_Unset,
        const TSubstObserver& substObserver = {});

    void SubstData(
        const TYVar* origin,
        TMacroData& macro,
        const TYVar* var,
        const TVars& vars,
        ECmdFormat cmdFormat,
        ESubstMode substMode,
        TString& result,
        ECmdFormat formatFor = ECF_Unset,
        const TSubstObserver& substObserver = {});

    void SetAllVarsNeedSubst(bool need) { AllVarsNeedSubst = need; }

    void WriteRequirements(TStringBuf reqs);

    static bool IsIncludeVar(const TStringBuf& cur);
    static bool IsReservedVar(const TStringBuf& cur, const TVars& vars);
    static bool IsGlobalReservedVar(const TStringBuf& cur, const TVars& vars);

private:
    template<typename T>
    void ApplyToOutputIncludes(T&& action) const {
        action(TStringBuf{}, GetOutputInclude());

        if (SpecFiles.index() == 1) {
            for (auto& [type, outputIncludes] : std::get<1>(SpecFiles).OutputIncludeForType) {
                action(type, outputIncludes);
            }
        } else {
            for (auto& [type, outputIncludes] : std::get<0>(SpecFiles).OutputIncludeForType) {
                action(type, outputIncludes.Data());
            }
        }
    }

    // Only input and output are changed outside of this class
    TSpecFileArr& GetAutoInput() { return std::get<1>(SpecFiles).AutoInput; }
    TSpecFileArr& GetOutputInclude() { return std::get<1>(SpecFiles).OutputInclude; }
    TSpecFileArr& GetTools() { return std::get<1>(SpecFiles).Tools; }
    TSpecFileArr& GetResults() { return std::get<1>(SpecFiles).Results; }

    template<typename T>
    void ApplyToOutputIncludes(T&& action) {
        action(TStringBuf{}, GetOutputInclude());
        for (auto& [type, outputIncludes] : std::get<1>(SpecFiles).OutputIncludeForType) {
            action(type, outputIncludes);
        }
    }

    bool InitDirs(TVarStrEx& curSrc, TModuleBuilder& mod, bool lastTry);
    // Finalizes TCommandInfo internal state (TSpecFileLists forming).
    // Switches TSpecFiles representation from TSpecFileList to TSpecFileArr.
    void Finalize();

    enum class EStructCmd {No, Yes};
    enum class EExprRole {Cmd, Var};

    ui64 InitCmdNode(const TYVar& var, EStructCmd structCmd, EExprRole role);
    void AddCmdNode(const TYVar& var, ui64 elemId, EStructCmd structCmd, EExprRole role);
    // TODO: move MsgPad here, too?
    TString SubstMacro(const TYVar* origin, TStringBuf pattern, TVector<TMacroData>& macros, ESubstMode substMode, const TVars& subst, ECmdFormat cmdFormat, ECmdFormat formatFor = ECF_Unset);
    void FillCoords(const TYVar* origin, TVector<TMacroData>& macros, ESubstMode substMode, const TVars& localVars, ECmdFormat cmdFormat, bool setAddCtxFilled = true);
    TString MacroCall(const TYVar* macroDefVar, const TStringBuf& macroDef, const TYVar* argsVar, const TStringBuf& args, ESubstMode substMode, const TVars& parentVars, ECmdFormat cmdFormat, bool convertNamedArgs);
    bool ApplyMods(const TYVar* valVar, TVarStr& value, const TYVar* modsVar, const TMacroData& macroData, ESubstMode substMode, const TVars& parentVars, ECmdFormat cmdFormat);

    void FillAddCtx(const TYVar& var, const TVars& parentVars);

    bool IsReservedVar(const TStringBuf& cur) const;
    bool IsGlobalReservedVar(const TStringBuf& cur) const;

    TString GetToolValue(const TMacroData& macroData, const TVars& vars);

    const TYVar* GetSpecMacroVar(const TYVar* origin, const TStringBuf& genericMacroname, const TStringBuf& args, const TVars& vars);

    void GetDirsFromOpts(const TStringBuf opt, const TVars& vars, THolder<TVector<TStringBuf>>& dst);
    void ApplyToolOptions(const TStringBuf macroName, const TVars& vars);

    void CollectVarsDeep(TCommands& commands, ui32 srcExpr, const TYVar& dstBinding, const TVars& varDefinitionSources);
    void ProcessGlobInput(TAddDepAdaptor& node, TStringBuf globStr);
};

void ParseRequirements(const TStringBuf requirements, THashMap<TString, TString>& result);
