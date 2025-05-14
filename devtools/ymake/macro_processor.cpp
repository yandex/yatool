#include "macro_processor.h"

#include "add_iter.h"
#include "add_node_context_inline.h"
#include "add_dep_adaptor.h"
#include "add_dep_adaptor_inline.h"
#include "args2locals.h"
#include "builtin_macro_consts.h"
#include "conf.h"
#include "module_state.h"
#include "module_builder.h"
#include "prop_names.h"
#include "exec.h"
#include "macro.h"
#include "macro_string.h"
#include "vars.h"
#include "ymake.h"

#include <devtools/ymake/common/npath.h>
#include <devtools/ymake/compact_graph/dep_types.h>
#include <devtools/ymake/compact_graph/query.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/options/static_options.h>
#include <devtools/ymake/symbols/symbols.h>

#include <util/folder/pathsplit.h>
#include <util/generic/algorithm.h>
#include <util/generic/fwd.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>
#include <util/stream/output.h>
#include <util/stream/str.h>
#include <util/string/cast.h>
#include <util/string/escape.h>
#include <util/string/split.h>
#include <util/string/subst.h>
#include <util/string/vector.h>
#include <util/system/compiler.h>
#include <util/system/types.h>
#include <util/system/yassert.h>

#include <stdlib.h>

#define SBDIAG YDIAG(SUBST) << MsgPad

namespace {

    TStringBuf MsgPad; // debug only

    TVarStrEx* FindMainElemOrDefault(std::span<TVarStrEx> elems, ui32 defaultElemPos) {
        if (elems.empty()) {
            return nullptr;
        }
        auto filterMain = [](const auto& elem) { return elem.Main; };
        auto it = FindIf(elems, filterMain);
        TVarStrEx* mainElem = nullptr;
        if (it != end(elems)) {
            mainElem = &*it;
            if (std::any_of(++it, end(elems), filterMain)) {
                YConfWarn(NoMain) << "Two or more main elements; picked the first one" << Endl;
            }
        } else if (defaultElemPos != Max<ui32>()) {
            if (elems.size() > 1) {
                YConfWarn(NoMain) << "No explicit main element; picked the default one" << Endl;
            }
            mainElem = &elems[defaultElemPos];
        }
        return mainElem;
    }

    bool OnlyMacroCall(const TVector<TMacroData>& macros) {
        for (const auto& macro : macros) {
            if (!macro.HasArgs && !macro.HasOwnArgs && !macro.ComplexArg) {
                return false;
            }
        }
        return !macros.empty();
    }

    bool OnlySelfCall(const TVector<TMacroData>& macros, const TStringBuf& pat) {
        return macros.size() == 1 && macros[0].SameName && macros[0].OrigFragment == pat;
    }

    inline bool MatchTags(const TVector<TVector<TStringBuf>>& macroTags, const TVector<TStringBuf> peerTags) {
        Y_ASSERT(!macroTags.empty());
        if (peerTags.empty()) {
            return false;
        }
        for (const auto& chunk : macroTags) {
            if (AllOf(chunk, [&peerTags] (const auto tag) { return FindPtr(peerTags, tag); })) {
                return true;
            }
        }
        return false;
    }

    const auto ConstrYDirNoDiag = [](NPath::EDirConstructIssue, const TStringBuf&) {};

    TString RemoveMod(EModifierFlag flag, TStringBuf expr) {
        if (!expr.starts_with("${") || !expr.ends_with('}')) {
            return TString{expr};
        }

        const TStringBuf mod = ModifierFlagToName(flag);
        const auto pos = expr.find(mod);
        if (pos == TStringBuf::npos) {
            return TString{expr};
        }
        Y_ASSERT(pos > 0 && pos < expr.size() -1); // expr was tested to have format "${.*}" earlier

        TString res{expr.substr(0, expr[pos - 1] == ';' ? pos - 1 : pos)};
        if (expr[pos - 1] != ';' && expr[pos + mod.size()] == ':') {
            res += expr.substr(pos + mod.size() + 1);
        } else {
            res += expr.substr(pos + mod.size());
        }
        return res;
    }

    // This function is using only for late outputs
    TString ApplyNamespaceModifier(TStringBuf buildRoot, TStringBuf ns, TStringBuf name, TStringBuf suffix) {
        Y_ASSERT(name.StartsWith(buildRoot));
        size_t slash = name.find(buildRoot);
        if (slash != TString::npos) {
            name = name.substr(slash + buildRoot.size() + 1);
        }
        return TString::Join(ns, name, suffix);
    }

    bool CheckForDirectory(const TVarStrEx& var, const TYVar& cmdVar, TStringBuf msg) {
        if (Y_UNLIKELY(var.IsDir)) {
            TStringBuf cmd, cmdName;
            ui64 id;
            ParseCommandLikeVariable(Get1(&cmdVar), id, cmdName, cmd);
            YConfErr(BadInput) << msg << " in " << cmdName << " " << var.Name << " is a directory! Won't be processed!" << Endl;
            return false;
        }
        return true;
    }
} // namespace

void ParseRequirements(const TStringBuf requirements, THashMap<TString, TString>& result) {
    TStringBuf before;
    TStringBuf after(requirements);
    while (after.TrySplit(' ', before, after)) {
        TStringBuf reqName;
        TStringBuf reqValue;
        if (before.TrySplit(':', reqName, reqValue)) {
            result[reqName] = reqValue;
        }
    }
    if (!after.empty()) {
        TStringBuf reqName;
        TStringBuf reqValue;
        if (after.TrySplit(':', reqName, reqValue)) {
            result[reqName] = reqValue;
        }
    }
}

bool IsInternalReservedVar(const TStringBuf& cur) {
    using namespace NVariableDefs;
    return EqualToOneOf(cur,
        VAR_INPUT,
        VAR_OUTPUT,
        VAR_AUTO_INPUT,
        VAR_PEERS,
        VAR_SRC,
        VAR_BINDIR,
        VAR_CURDIR,
        VAR_ARCADIA_BUILD_ROOT,
        VAR_ARCADIA_ROOT,
        VAR_SRCS_GLOBAL,
        VAR_YMAKE_BIN,
        VAR_TARGET,
        VAR_MANAGED_PEERS,
        VAR_MANAGED_PEERS_CLOSURE,
        VAR_GLOBAL_TARGET,
        VAR_PEERS_LATE_OUTS,
        VAR_ALL_SRCS);
}

TCommandInfo::TCommandInfo(const TBuildConfiguration& conf, TDepGraph* graph, TUpdIter* updIter, TModule* module)
    : Conf(&conf)
    , Graph(graph)
    , UpdIter(updIter)
    , Module(module)
{
}

TCommandInfo::TCommandInfo()
    : Conf(nullptr)
    , Graph(nullptr)
    , UpdIter(nullptr)
    , Module(nullptr)
{
}

void TCommandInfo::SetCommandSink(TCommands* commands) {
    CommandSink = commands;
}

void TCommandInfo::SetCommandSource(const TCommands* commands) {
    CommandSource = commands;
}

// macroDef is "(X Y Z)zzz"
inline TString TCommandInfo::MacroCall(const TYVar* macroDefVar, const TStringBuf& macroDef, const TYVar* modsVar, const TStringBuf& args, ESubstMode substMode, const TVars& vars, ECmdFormat cmdFormat, bool convertNamedArgs) {
    SBDIAG << "MacroCall: macroDef = " << macroDef << ", args = " << args << Endl;
    const ESubstMode nmode = substMode == ESM_DoSubst ? ESM_DoBothCm : ESM_DoBoth; // hack :(

    TVector<TMacroData> macros;
    GetMacrosFromPattern(args, macros, false);


    // We need to deeply substitute actual arguments for macro calls
    // AllVarsNeedSubst forces recursion in ApplyMods by call to SubstMacro
    // This workaround cures the problem induced by SET_APPEND macro
    // SET_APPEND(VAR x) => "2:VAR=$VAR x"
    // SOME_MACRO($VAR)
    // If we do not fully substitute arguments for macro call SOME_MACRO here, then
    // prepArgs == "($VAR x)" and it is not what is really expected here
    bool saveAllVarsNeedSubst = AllVarsNeedSubst;
    AllVarsNeedSubst = true;
    const TString prepArgs = SubstMacro(modsVar, args, macros, nmode, vars, cmdFormat);
    AllVarsNeedSubst = saveAllVarsNeedSubst;

    TStringBuf macroName = GetCmdName(Get1(macroDefVar));
    ApplyToolOptions(macroName, vars);

    auto blockDataIt = Conf->BlockData.find(macroName);
    const TBlockData* blockData
        = blockDataIt != Conf->BlockData.end()
        ? &blockDataIt->second
        : nullptr;

    TVector<TStringBuf> argNames;
    if (!SplitArgs(macroDef, argNames)) {
        throw yexception() << "MacroCall: no args in '" << macroDef << "'" << Endl;
    }
    TVars ownVars(&vars);
    ownVars.Id = vars.Id;

    TVector<TStringBuf> tempArgs;
    if (!SplitArgs(prepArgs, tempArgs)) {
        TStringBuilder s;
        if (modsVar) {
            s << " ";
            for (const auto& v: *modsVar) s << v.Name << " ";
        }
        throw yexception() << "Expected argument list in () brackets, got [" << prepArgs << "] in [" << s << "]";
    }
    AddMacroArgsToLocals(
        convertNamedArgs && blockData && blockData->CmdProps ? blockData->CmdProps.get() : nullptr,
        argNames,
        tempArgs,
        ownVars,
        *Conf->GetStringPool()
    ).or_else([&](const TMapMacroVarsErr& err) -> std::expected<void, TMapMacroVarsErr> {
        err.Report(prepArgs);
        throw yexception() << "MapMacroVars failed" << Endl;
    }).value();

    for (auto& var : ownVars)
        var.second.NoInline = true;

    if (blockData && blockData->CmdProps) {

        // enable inlining for macro arguments like `IN{input}[]` etc.

        // rationale: these arguments are delivered as `TVarStr` arrays,
        // and the inliner can handle this,
        // whereas the preevaluator currently cannot (see TBD/`Eval1` in `TRefReducer::Evaluate(NPolexpr::EVarId)`)

        auto hasDeepReplacement = false;
        for (const auto& [name, kw] : blockData->CmdProps->GetKeywords()) {
            if (kw.DeepReplaceTo.size() != 0) {
                ownVars[name].NoInline = false;
                hasDeepReplacement = true;
            }
        }

        // enable vararg inlining for macros with "deep replacements";
        // use cases to consider:
        // we want this in `macro RUN_PROGRAM(<blah>, Args...) {...}`;
        // we do NOT want this in `macro _SRC(EXT, SRC, SRCFLAGS...) {...}`;
        // we do NOT want this in `macro SRC_C_SSE2(FILE, FLAGS...) {...}` and suchlike;

        // rationale: varargs are subject to deep replacement,
        // and the preevaluator needs to observe the resulting input/output/tool modifiers
        // directly in the top-level expression

        bool hasVarArg = !argNames.empty() && argNames.back().EndsWith(NStaticConf::ARRAY_SUFFIX); // FIXME: this incorrectly reports "true" for "macro M(X[]){...}" and suchlike
        if (hasVarArg && hasDeepReplacement) {
            TStringBuf name = argNames.back();
            name.Chop(3);
            ownVars[name].NoInline = false;
        }

    }

    if (blockData && blockData->StructCmd) {
        Y_ASSERT (CommandSink);
        auto command = MacroDefBody(macroDef);
        TSpecFileList* knownInputs = {};
        TSpecFileList* knownOutputs = {};
        if (auto specFiles = std::get_if<0>(&SpecFiles)) {
            knownInputs = &specFiles->Input;
            knownOutputs = &specFiles->Output;
        }
        auto compiled = CommandSink->Compile(command, *Conf, ownVars, true, {.KnownInputs = knownInputs, .KnownOutputs = knownOutputs});
        const ui32 cmdElemId = CommandSink->Add(*Graph, std::move(compiled.Expression));
        GetCommandInfoFromStructCmd(*CommandSink, cmdElemId, compiled.Inputs.Take(), compiled.Outputs.Take(), compiled.OutputIncludes.Take(), ownVars);
        auto res = Graph->Names().CmdNameById(cmdElemId).GetStr();
        return TString(res);
    }

    TVector<TMacroData> bMacros;
    GetMacrosFromPattern(macroDef, bMacros, false);
    return SubstMacro(macroDefVar, MacroDefBody(macroDef), bMacros, substMode, ownVars, cmdFormat);
}

TString GlueCmd(std::span<const TStringBuf> args) {
    if (args.empty()) {
        return "()";
    }

    TString result;
    result.reserve(
        2 + // trailing and leading brackets "(" ")"
        args.size() - 1  + // single space delimeters between args
        std::accumulate(args.begin(), args.end(), 0u, [](size_t val, TStringBuf arg) {
            return val + (arg.empty() ? 2 : arg.size());
        })
    );

    result.push_back('(');
    result.append(args.front());
    for (TStringBuf arg: args.subspan(1)) {
        result.push_back(' ');
        if (arg.empty()) {
            result.append("\"\""sv);
        } else {
            result.append(arg);
        }
    }
    result.push_back(')');

    return result;
}

inline TNodeAddCtx *GetAddCtx(const TYVar& var) {
    return var.EntryPtr && var.EntryPtr->second.AddCtx ? var.EntryPtr->second.AddCtx : nullptr;
}

bool TCommandInfo::GetCommandInfoFromPluginCmd(const TMacroCmd& cmd, const TVars& vars, TModule& mod) {
    cmd.Output(GetOutputInternal());
    cmd.OutputInclude(GetOutputIncludeInternal());
    cmd.Input(GetInputInternal());

    TVector<TString> tools;
    cmd.Tools(tools);

    for (auto& tool : tools) {
        TVarStr file(tool);
        file.FromLocalVar = true;
        GetToolsInternal().Push(std::move(file));
    }

    Cmd.SetSingleVal("PLUGIN", cmd.ToString(), mod.GetId(), vars.Id);
    InitCmdNode(Cmd);
    FillAddCtx(Cmd, vars);

    return true;
}

THashMap<TString, TString> TCommandInfo::TakeRequirements() {
    if (Requirements) {
        THashMap<TString, TString> res;
        res.swap(*Requirements);
        return res;
    }
    if (Conf) {
        return Conf->GetDefaultRequirements();
    }
    return THashMap<TString, TString>();
}

ui64 TCommandInfo::InitCmdNode(const TYVar& var) {
    const ui64 elemId = Graph->Names().AddName(EMNT_BuildCommand, Get1(&var));
    // we can have duplicate command entries even within a single Makefile
    // e.g. SRCS(foo/a.cpp bar/a.cpp) may turn into `SRCScxx cpp cc=(a.cpp)' for both files
    if (var.EntryPtr) {
        return elemId;
    }
    AddCmdNode(var, elemId);
    return elemId;
}

void TCommandInfo::AddCmdNode(const TYVar& var, ui64 elemId) {
    Y_ENSURE(UpdIter != nullptr);
    TModule* module = var.Id ? Module : UpdIter->ParentModule;
    TUpdEntryPtr entry = &*UpdIter->Nodes.Insert(MakeDepsCacheId(EMNT_BuildCommand, elemId), &UpdIter->YMake, module);
    var.EntryPtr = entry;
    entry->second.AddCtx->ElemId = elemId;
    if (/*true*/ entry->second.AddCtx->Module == module) { // TODO: fix this condition. Id must be module id, not Makefile id.
        entry->second.SetReassemble(true);
        entry->second.SetOnceEntered(false);
    }
}

void TCommandInfo::CollectVarsDeep(TCommands& commands, ui32 srcExpr, const TYVar& dstBinding, const TVars& varDefinitionSources) {

    //
    // srcExpr (an elemId) points to an "S:123" build command element,
    //     which is visible in the graph for actual commands,
    //     and invisible (not referenced directly) for context variable content
    //
    // dstBinding points to
    //     either an "S:123" build command node (as an implicit .CMD=... "binding"),
    //     or a "0:VARNAME=S:123" context variable node
    //

    auto compilationFallback = [&](const std::exception& e, TStringBuf varName, TStringBuf varValue) {
        YConfErr(Details)
            << "Expression error (module " << Module->GetUserType() << "), "
            << varName << "=" << varValue << ": "
            << e.what() << Endl;
        return commands.Compile("$FAIL_EXPR", *Conf, Module->Vars, false, {});
    };

    auto mkCmd = [&](TStringBuf exprVarName) {
        ui64 id;
        TStringBuf cmdName;
        TStringBuf cmdValue;
        ParseCommandLikeVariable(varDefinitionSources.Get1(exprVarName), id, cmdName, cmdValue);
        auto compiled = NCommands::TCompiledCommand();
        try {
            compiled = commands.Compile(cmdValue, *Conf, varDefinitionSources, false, {});
        } catch (const std::exception& e) {
            compiled = compilationFallback(e, exprVarName, cmdValue);
        }
        // TODO: there's no point in allocating cmdElemId for expressions
        // that do _not_ have directly corresponding nodes
        // (and are linked as "0:VARNAME=S:123" instead)
        auto cmdElemId = commands.Add(*Graph, std::move(compiled.Expression));
        return std::make_tuple(cmdName, Graph->Names().CmdNameById(cmdElemId).GetStr(), id);
    };

    auto exprVars = commands.GetCommandVars(srcExpr);
    for (auto&& exprVarName : exprVars) {

        auto isGlobalReservedVar = IsGlobalReservedVar(exprVarName);

        if (isGlobalReservedVar) {
            GetAddCtx(dstBinding)->AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty(NProps::USED_RESERVED_VAR, exprVarName));
        }

        if (exprVarName.ends_with("__LATEOUT__")) {
            auto [_ignore_cmdName, value, _ignore_id] = mkCmd(exprVarName);
            GetAddCtx(dstBinding)->AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty(NProps::LATE_OUT, value));
        }

        auto var = varDefinitionSources.Lookup(exprVarName);

        if (!var || var->DontExpand)
            continue;
        if (var->size() == 0)
            continue;

        // TODO: should `IsInternalReservedVar` matter here?

        if (isGlobalReservedVar) {
            if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes) {
                auto [it, added] = GetOrInit(GlobalVars).emplace(exprVarName, TYVar{});
                if (added) {
                    auto [cmdName, value, id] = mkCmd(exprVarName);
                    it->second.SetSingleVal(cmdName, value, id);
                }
            }

            continue;
        }

        auto [it, added] = GetOrInit(LocalVars).emplace(exprVarName, TYVar{});
        if (!added)
            continue;
        auto& subBinding = it->second;

        auto val = EvalAll(var);
        auto compiled = NCommands::TCompiledCommand();
        try {
            compiled = commands.Compile(val, *Conf, varDefinitionSources, false, {});
        } catch (const std::exception& e) {
            compiled = compilationFallback(e, exprVarName, val);
        }
        const ui32 subExpr = commands.Add(*Graph, std::move(compiled.Expression));
        auto subExprRef = Graph->Names().CmdNameById(subExpr).GetStr();

        subBinding.SetSingleVal(exprVarName, subExprRef, 0);
        InitCmdNode(subBinding);
        CollectVarsDeep(commands, subExpr, subBinding, varDefinitionSources);

    }

}

bool TCommandInfo::GetCommandInfoFromStructCmd(
    TCommands& commands,
    ui32 cmdElemId,
    std::span<const NCommands::TCompiledCommand::TInput> cmdInputs,
    std::span<const NCommands::TCompiledCommand::TOutput> cmdOutputs,
    std::span<const NCommands::TCompiledCommand::TOutputInclude> cmdOutputIncludes,
    const TVars& vars
) {

    AddCmdNode(Cmd, cmdElemId);
    Cmd.SetSingleVal(Graph->Names().CmdNameById(cmdElemId).GetStr(), true);

    for (const auto& input : cmdInputs) {
        TVarStrEx in(input.Name);
        if (input.Context_Deprecated) [[unlikely]] {
            if (NPath::IsLink(in.Name)) {
                // ...as built by the "input" modifier;
                // strip and redo
                in.Name = NPath::GetTargetFromLink(in.Name);
                if (NPath::IsTypedPath(in.Name) && NPath::GetType(in.Name) == NPath::Unset)
                    in.Name = NPath::CutType(in.Name);
            }
            in.Name = TFileConf::ConstructLink(input.Context_Deprecated, NPath::ConstructPath(in.Name)); // lifted from TCommandInfo::ApplyMods
        }
        in.IsGlob = input.IsGlob;
        in.IsMacro = input.IsLegacyGlob;
        in.ResolveToBinDir = input.ResolveToBinDir;
        GetInputInternal().Push(in);
    }

    for (const auto& output : cmdOutputs) {
        auto ix = GetOutputInternal().Push(output.Name).first;
        GetOutputInternal().Update(ix, [output](auto& var) {
            var.IsTmp |= output.IsTmp;
            var.IsGlobal |= output.IsGlobal;
            var.NoAutoSrc |= output.NoAutoSrc;
            var.NoRel |= output.NoRel;
            var.ResolveToBinDir |= output.ResolveToBinDir;
            var.AddToIncl |= output.AddToIncl;
            var.Main |= output.Main;
        });
    }

    for (const auto& outputInclude : cmdOutputIncludes) {
        auto ix = GetOutputIncludeInternal().Push(outputInclude.Name).first;
        GetOutputIncludeInternal().Update(ix, [outputInclude](auto& var) {
            var.OutInclsFromInput |= outputInclude.OutInclsFromInput;
        });
    }

    CollectVarsDeep(commands, cmdElemId, Cmd, vars);

    return true;
}

bool TCommandInfo::GetCommandInfoFromMacro(const TStringBuf& realMacroName, EMacroType type, const TVector<TStringBuf>& args, const TVars& vars, ui64 id) {
    // Take appropriate macro specialization
    const TStringBuf& macroName = Conf->GetSpecMacroName(realMacroName, args);
    const TYVar* macroVar = vars.Lookup(macroName);
    const TString pattern(Get1(macroVar));
    if (!pattern) {
        YConfErr(BadMacro) << "Trying to use undefined macro " << macroName << " in a command" << Endl;
        return false;
    }
    const ui64 jElemId = InitCmdNode(*macroVar);
    SBDIAG << "GetCommandInfoFromMacro " << macroName << " " << pattern << "\n";

    if (type == EMT_Usual) {
        Cmd.SetSingleVal(macroName, TString::Join("$", macroName), id, vars.Id);
        Cmd.BaseVal = macroVar;
        InitCmdNode(Cmd);
        SubstMacro(macroVar, pattern, ESM_DoFillCoord, vars, ECF_Unset, true);
    } else {
        Y_ASSERT(type == EMT_MacroCall);
        Cmd.SetSingleVal(macroName, GlueCmd(args), id, vars.Id);
        Cmd.BaseVal = macroVar;
        InitCmdNode(Cmd);
        MacroCall(macroVar, GetCmdValue(pattern), &Cmd, GetCmdValue(Get1(&Cmd)), ESM_DoFillCoord, vars, ECF_Unset, false);
    }
    GetAddCtx(Cmd)->AddDep(EDT_Include, EMNT_BuildCommand, jElemId);

    SBDIAG << "Macro '" << pattern << "', input: " << GetInputInternal().size() << ", output: " << GetOutputInternal().size() << ", includes: " << GetOutputIncludeInternal().size() << " cmd: " << Get1(&Cmd) << " id: " << Cmd.Id << "\n";

    return true;
}

void TCommandInfo::InitFromModule(const TModule& mod) {
    InputDir = mod.GetDir();
    BuildDir = Graph->Names().FileConf.ReplaceRoot(InputDir, NPath::Build); // InitDirs() may rewrite these
    InputDirStr = InputDir.GetTargetStr();
    BuildDirStr = BuildDir.GetTargetStr(); // InitDirs() may rewrite these
}

bool TCommandInfo::Init(const TStringBuf& sname, TVarStrEx& src, const TVector<TStringBuf>* args, TModuleBuilder& modBuilder) {
    TModule& mod = modBuilder.GetModule();
    InitFromModule(mod);
    TStringBuf macroName = src.Name;
    TVector<TStringBuf> sectionArg;

    if (Conf->RenderSemantics && src.IsMacro) {
        const auto fres = Conf->BlockData.find(macroName);
        if (fres != Conf->BlockData.end() && !fres->second.HasSemantics) {
            YConfErr(NoSem) << "No semantics specified for macro " << macroName << " in module " << mod.GetUserType() << ". It is not intended for export." << Endl;
        }
    }

    if (!src.IsMacro) {
        src.ForIncludeOnly = src.ForIncludeOnly || Conf->IsIncludeOnly(src.Name);

        macroName = Conf->GetMacroByExt(sname, macroName);
        if (args && (*args).size() > 1) {
            sectionArg.assign(args->begin(), args->end() - 1);
        }

        sectionArg.push_back(NPath::Basename(src.Name));

        Y_ASSERT(sectionArg.back().size());
        args = &sectionArg;
        YDIAG(DG) << "Search for macrocalls for: " << macroName << Endl;
        modBuilder.ProcessConfigMacroCalls(macroName, *args);
    }

    bool ok = false;

    if (!macroName.empty()) {
        // this block data pattern detector's job is to handle
        // new-style commands directly embedded in macro specializations,
        // the primary use case being
        // `macro _SRC("ext", SRC, SRCFLAGS...) {... .STRUCT_CMD=yes ...}`
        auto macroVar = mod.Vars.Lookup(macroName);
        auto macroVal = Get1(macroVar);
        auto pattern = GetCmdValue(macroVal);
        bool structCmd = false;

        if (auto it = Conf->BlockData.find(macroName); it != Conf->BlockData.end()) {
            // the `SRC(...)` case (plus `SRC_C_PIC` and other similar variations);
            // causes one round of `MacroCall()` to expand `_SRC____ext`
            if (it->second.StructCmd)
                structCmd = true;
        }
        else {
            // the `SRCS(...)` case;
            // causes two rounds of `MacroCall()` to expand `SRCSext` then `_SRC____ext`
            TVector<TMacroData> macros;
            GetMacrosFromPattern(pattern, macros, false);
            for (auto&& m : macros) {
                if (!m.HasArgs)
                    continue;
                auto it = Conf->BlockData.find(m.Name);
                if (it == Conf->BlockData.end())
                    continue;
                if (!it->second.StructCmd)
                    continue;
                structCmd = true;
                break;
            }
        }
        if (structCmd) {
            // pass the torch to the `if (...StructCmd...)` section in `MacroCall()`
            Cmd.SetSingleVal(macroName, GlueCmd(*args), mod.GetId(), mod.Vars.Id);
            Cmd.BaseVal = macroVar;
            // TBD: the `convertNamedArgs` argument policy; by the old macro processing logic,
            // when we use `SRCS(name.ext)`, the `_SRC____ext` specialization macro is expanded via
            //     GetCommandInfoFromMacro -> MacroCall -> ... -> ApplyMods -> *MacroCall*,
            // and `convertNamedArgs` is true;
            // when we use `SRC(name.ext)`, the same macro is expanded via
            //     GetCommandInfoFromMacro -> *MacroCall*,
            // and `convertNamedArgs` is false
            auto ignored = MacroCall(macroVar, pattern, &Cmd, GetCmdValue(Get1(&Cmd)), ESM_DoSubst, mod.Vars, ECF_ExpandVars, false);
            ok = true;
        }
    }

    if (!ok) {
        ok = macroName.size() && GetCommandInfoFromMacro(macroName, EMT_MacroCall, *args, mod.Vars, mod.GetId());
    }
    if (!ok) {
        if (src.IsMacro) {
            YConfWarn(BadMacro) << "macro could not be called: " << src.Name << Endl;
        } else {
            if (sname != "SRCS" || !src.ForIncludeOnly) {
                src.ByExtFailed = true;
            }
            // here we'll have empty Cmd which means that we should add
            // Inputs to Module via EDT_Search
            MainInputCandidateIdx = static_cast<ui32>(GetInputInternal().Push(src).first);
            return true;
        }
        return false;
    }

    if (!src.IsMacro) {
        TVarStrEx in(src);
        TStringBuf inputName = NPath::Basename(in.Name);
        size_t inputIdx = GetInputInternal().Index(inputName);
        if (inputIdx != NPOS) {
            in.MergeFlags(GetInputInternal()[inputIdx]);
            const bool replaced = GetInputInternal().Replace(inputIdx, in);
            Y_ASSERT(replaced);
        } else {
            if (ok && src.IsPathResolved) {
                YConfWarn(NoInput) << "macro for " << src.Name << " didn't use it as input" << Endl;
            }
            const auto [_idx, inputAdded] = GetInputInternal().Push(in);
            Y_ASSERT(inputAdded);
            inputIdx = _idx;
        }
        MainInputCandidateIdx = static_cast<ui32>(inputIdx);
        HasGlobalInput = in.IsGlobal;
    }

    // Apply .PEERDIR and .ADDINCL from macro
    if (AddPeers) {
        SBDIAG << "ADDPEERS[" << AddPeers->size() << "] from " << Get1(&Cmd) << " added to module " << modBuilder.UnitName() << Endl;
        size_t i = 0;
        for (const auto& peer : *AddPeers) {
            SBDIAG << "  [" << i++ << "] " << peer << Endl;
        }
        modBuilder.DirStatement(NMacro::PEERDIR, *AddPeers);
        AddPeers.Reset();
    }
    if (AddIncls) {
        SBDIAG << "ADDINCLS[" << AddIncls->size() << "] from " << Get1(&Cmd) << " added to module " << modBuilder.UnitName() << Endl;
        size_t i = 0;
        for (const auto& incl : *AddIncls) {
            SBDIAG << "  [" << i++ << "] " << incl << Endl;
        }
        modBuilder.DirStatement(NMacro::ADDINCL, *AddIncls);
        AddIncls.Reset();
    }
    return true;
}


bool TCommandInfo::InitDirs(TVarStrEx& curSrc, TModuleBuilder& modBuilder, bool lastTry) {
    const TModule& mod = modBuilder.Module;
    if (!modBuilder.ResolveSourcePath(curSrc, mod.GetDir(), lastTry ? TModuleResolver::LastTry : TModuleResolver::Default)) {
        return lastTry;
    }

    auto& fileConf = Graph->Names().FileConf;
    InputDirStr = NPath::Parent(curSrc.Name);
    InputDir = InputDirStr.empty() ? TFileView() : fileConf.GetStoredName(InputDirStr);
    BuildDirStr = NPath::SetType(mod.GetDir().GetTargetStr(), NPath::Build);
    if (InputDirStr.size()) {
        const TString buildPath = NPath::SetType(InputDirStr, NPath::Build);
        TString relative = NPath::Relative(buildPath, BuildDirStr);
        if (relative) {
            auto transform = !curSrc.NoTransformRelativeBuildDir;
            if (transform) {
                SubstGlobal(relative, "..", "__");
            }
            if (relative.StartsWith(transform ? "__" : "..")) {
                // Source path and module path are independent
                BuildDirStr = NPath::Join(BuildDirStr, relative);
            } else {
                // Source is in subdir of the module dir
                BuildDirStr = NPath::Join(BuildDirStr, transform ? "_" : ".", relative);
            }
        }
    }
    BuildDir = fileConf.GetStoredName(BuildDirStr);
    return true;
}

void TCommandInfo::Finalize() {
    if (SpecFiles.index() == 0) {
        TSpecFileLists& lists = std::get<0>(SpecFiles);
        TSpecFileArrs arrs;
        arrs.Input = lists.Input.Take();
        arrs.AutoInput = lists.AutoInput.Take();
        arrs.Output = lists.Output.Take();
        arrs.OutputInclude = lists.OutputInclude.Take();
        arrs.Tools = lists.Tools.Take();
        arrs.Results = lists.Results.Take();
        for (auto& [type, output_includes] : lists.OutputIncludeForType) {
            arrs.OutputIncludeForType[type] = output_includes.Take();
        }
        SpecFiles = std::move(arrs);
    }
}

TCommandInfo::ECmdInfoState TCommandInfo::CheckInputs(TModuleBuilder& modBuilder, TAddDepAdaptor& inputNode, bool lastTry) {
    Finalize();

    MainInput = FindMainElemOrDefault(GetInput(), MainInputCandidateIdx);
    if (MainInput && !InitDirs(*MainInput, modBuilder, lastTry)) {
        YDIAG(VV) << "Main input in " << Get1(&Cmd) << " is not ready, delay processing" << Endl;
        return FAILED;
    }

    for (auto& input : GetInput()) {
        if (input.IsGlob) {
            continue;
        }
        const TString origInput = input.Name;
        if (!modBuilder.ResolveSourcePath(input, InputDir, lastTry ? TModuleBuilder::LastTry : TModuleBuilder::Default) && !lastTry) {
            YDIAG(VV) << "Input '" << input.Name << "' in " << Get1(&Cmd) << " is not ready, delay processing" << Endl;
            return FAILED;
        }

        if (!input.DirAllowed && !CheckForDirectory(input, Cmd, "input dependency"sv)) {
            return SKIPPED;
        }

        EMakeNodeType inputType = input.IsMacro
            ? EMNT_UnknownCommand
            : input.IsDir
                ? EMNT_Directory
                : (input.IsOutputFile ? EMNT_NonParsedFile : FileTypeByRoot(input.Name));

        Y_ASSERT(input.ElemId); // must exists if ResolveSourcePath is true

        modBuilder.SaveInputResolution(input, origInput, InputDir);

        TAddDepAdaptor& node = inputNode.AddOutput(input.ElemId, inputType, false);

        if (!input.ByExtFailed) {
            auto& modData = node.GetModuleData();
            modData.UsedAsInput = true;
            if (modData.BadCmdInput) {
                if (input.IsOutputFile) {
                    YConfInfo(BadAuto) << "`noauto' flag needed for " << input.Name << Endl;
                } else {
                    YConfInfo(BadAuto) << "file must not be added by extension: " << input.Name << Endl;
                }

                modData.BadCmdInput = false; // do not output this multiple times
            }
        }
        //UpdIter->RecursiveAddNode(ci->NotFound? EMNT_MissingFile : EMNT_File, ci->Name, this);
    }

    TCommandInfo::ECmdInfoState state = OK;
    ApplyToOutputIncludes([&](TStringBuf type, TSpecFileArr& outputIncludes) {
        Y_UNUSED(type);

        for (auto& outputInclude : outputIncludes) {
            if (outputInclude.OutInclsFromInput) {
                // Try to resolve as input immediately
                modBuilder.ResolveSourcePath(outputInclude, InputDir, TModuleResolver::LastTry);
                if (!CheckForDirectory(outputInclude, Cmd, "output include"sv)) {
                    state = SKIPPED;
                    return;
                }
            } else {
                // Only try to resolve as known by default (without FS check),
                // delay actual resolvig as include until InducedDeps property is applied.
                modBuilder.ResolveAsKnownWithoutCheck(outputInclude);
                auto resolveFile = modBuilder.MakeUnresolved(outputInclude.Name);
                outputInclude.Name = modBuilder.GetStr(resolveFile);
                outputInclude.ElemId = resolveFile.GetElemId();
            }
            Y_ASSERT(outputInclude.ElemId); // Must be filled in all ways
        }
    });

    return state;
}

bool TCommandInfo::Process(TModuleBuilder& modBuilder, TAddDepAdaptor& inputNode, bool finalTargetCmd) {
    TModule& mod = modBuilder.GetModule();
    Y_ENSURE(UpdIter != nullptr);
    const size_t startCountOuts = finalTargetCmd ? 0 : 1;
    size_t numRealOut = finalTargetCmd ? 1 : 0;

    TVersionedCmdId curCmdId(Cmd.EntryPtr ? ElemId(Cmd.EntryPtr->first) : 0);
    TStringBuf curCmdName = Get1(&Cmd);
    YDIAG(Dev) << "Process command: " << curCmdName << Endl;

    TFileConf& fileConf = Graph->Names().FileConf;

    Y_ASSERT(!fileConf.GetName(inputNode.ElemId).IsLink());
    TStringBuf inputNodeName = fileConf.GetName(inputNode.ElemId).GetTargetStr();

    const auto& ownEntries = mod.GetOwnEntries();
    for (auto& output : GetOutput()) {
        if (!modBuilder.FormatBuildPath(output, InputDir, BuildDir)) {
            YConfErr(BadOutput) << "Directory " << output.Name << " is not allowed as output. Skip command: "
                                << SkipId(curCmdName) << Endl;
            return false;
        }
        YDIAG(Dev) << "Process: AddName " << output.Name << Endl;

        if (const auto fid = fileConf.GetIdNx(output.Name)) {
            if (ownEntries.has(fid)) {
                YConfErr(DupSrc) << output.Name << " was already added in this project. Skip command: "
                                 << SkipId(curCmdName) << Endl;
                return false;
            }

            // We do not consider outputs of module command that macth main module output as a DupSrc issue
            if (!mod.IgnoreDupSrc() && !(finalTargetCmd && fid == mod.GetId())) {
                if (UpdIter->CheckNodeStatus({EMNT_NonParsedFile, fid}) == NGraphUpdater::ENodeStatus::Ready && !mod.GetSharedEntries().has(fid)) {
                    ConfMsgManager()->AddDupSrcLink(fid, mod.GetId());
                }
            }

            const TNodeId id = Graph->GetFileNodeById(fid).Id();
            if (id == TNodeId::Invalid && !finalTargetCmd && inputNodeName == output.Name) {
                YConfErr(BadOutput) << "The name of intermediate output " << output.Name
                                    << " matches the module name. Skip command: " << SkipId(curCmdName) << Endl;
                return false;
            }

            if (id != TNodeId::Invalid && Graph->GetFileNodeData(fid).NodeModStamp == fileConf.TimeStamps.CurStamp()) {
                TNodeId cmdId = GetDepNodeWithType(id, *Graph, EDT_BuildCommand, EMNT_BuildCommand);
                if (cmdId == TNodeId::Invalid) {
                    // `finalTargetCmd` means either module command or global command, so we cannot just take modBuilder.GetNode().Id
                    TNodeId mainId = GetDepNodeWithType(id, *Graph, EDT_OutTogether, finalTargetCmd ? modBuilder.GetNode().NodeType : EMNT_NonParsedFile);
                    if (finalTargetCmd && mainId == TNodeId::Invalid) {
                        // Second try for global command
                        mainId = GetDepNodeWithType(id, *Graph, EDT_OutTogether, EMNT_NonParsedFile);
                    }
                    Y_ASSERT(mainId != TNodeId::Invalid);
                    cmdId = GetDepNodeWithType(mainId, *Graph, EDT_BuildCommand, EMNT_BuildCommand);
                    Y_ASSERT(cmdId != TNodeId::Invalid);
                }
                const auto cmdView = Graph->GetCmdName(Graph->Get(cmdId));
                if (cmdView.IsNewFormat() || curCmdId.IsNewFormat()) {
                    // TBD
                } else {
                    const auto cmdName = cmdView.GetStr();
                    static constexpr TStringBuf touchCmd = "TOUCH";
                    static constexpr TStringBuf initPy = "__init__.py";
                    if (GetCmdValue(cmdName) != GetCmdValue(curCmdName) &&
                            !(GetCmdName(cmdName) == touchCmd && GetCmdName(curCmdName) == touchCmd && NPath::Basename(output.Name) == initPy)) {
                        YConfErr(BUID) << "Two different commands want to produce the output " << output.Name << ": "
                                        << SkipId(cmdName) << " vs " << SkipId(curCmdName) << Endl;
                    }
                }
            }
        }
        if (!output.ElemId) {
            output.ElemId = Graph->Names().AddName(EMNT_File, output.Name);
            output.OutputInThisModule = true;
        }

        if (output.AddToModOutputs) {
            mod.ExtraOuts.push_back(output.ElemId);
        }

        // This IncDir should basically enable only resolving of the exact output marked by `addincl` modifier
        // There is no way to apply such precise filtering, but it is OK to add IncDir so late since the
        // resolving subject is added to the graph later in this function.
        if (output.AddToIncl) {
            TStringBuf incl = NPath::Parent(output.Name);
            // TODO: add only if included via "" from .h
            modBuilder.AddIncdir(incl, EIncDirScope::Global, false);
        }
        numRealOut += !output.IsTmp;
    }

    if (!Cmd) {
        return true;
    }

    if (!numRealOut) {
        YConfErr(NoOutput) << "macro " << SkipId(curCmdName) << " resulted in no outputs, can't add to graph" << Endl;
        return false;
    }

    // Determining the main output.
    ui64 mainOutId = 0;
    EMakeNodeType mainOutType = EMNT_NonParsedFile;
    if (finalTargetCmd) {
        mainOutId = inputNode.ElemId;
        mainOutType = inputNode.NodeType;
    } else {
        Y_ASSERT(!MainOutput);
        MainOutput = FindMainElemOrDefault(GetOutput(), 0);
        Y_ASSERT(MainOutput);
        MainOutput->IsGlobal = MainOutput->IsGlobal || HasGlobalInput;
        mainOutId = MainOutput->ElemId;
    }

    ui64 cmdElemId = Graph->Names().AddName(EMNT_BuildCommand, curCmdName);

    // 0. Prepare OUTPUT_INCLUDES nodes (ParsedIncls.*)
    THashMap<TStringBuf, TCreateParsedInclsResult> outputIncludeForType;
    ApplyToOutputIncludes([&](TStringBuf type, const TSpecFileArr& outputIncludeArr){
        if (type.empty()) {
            type = "*";
        }

        TVector<TResolveFile> outputIncludes(Reserve(outputIncludeArr.size()));

        for (const auto& outputInclude : outputIncludeArr) {
            YDIAG(DG) << "Include dep: " << outputInclude.Name << " type: " << type << Endl;
            Y_ASSERT(NPath::IsTypedPath(outputInclude.Name));
            outputIncludes.emplace_back(modBuilder.AssumeResolved(outputInclude.Name));
        }

        outputIncludeForType.emplace(type, TNodeAddCtx::CreateParsedIncls(
            Module, *Graph, *UpdIter, UpdIter->YMake,
            EMNT_BuildCommand, cmdElemId, type, outputIncludes
        ));
    });

    auto addOutputIncludes = [&](TAddDepAdaptor& addCtx) {
        for (const auto& [_, parsedIncludes]: outputIncludeForType) {
            if (auto* node = parsedIncludes.Node) {
                addCtx.AddDepIface(EDT_Property, node->NodeType, node->ElemId);
            }
        }
    };

    const bool hasExtraOuts = GetOutput().size() > startCountOuts;
    const bool mainOutAsExtra = hasExtraOuts && Conf->MainOutputAsExtra() && !IsModuleType(mainOutType);

    const bool addModuleNode = Conf->DedicatedModuleNode() && IsModuleType(mainOutType);
    EMakeNodeType moduleType = EMNT_Last;
    TAddDepAdaptor* moduleNode = nullptr;

    auto makeMainNodes = [&]() {
        EMakeNodeType fileNodeType = mainOutType;

        Y_ASSERT(!fileConf.GetName(mainOutId).IsLink());
        TStringBuf mainOutName = fileConf.GetName(mainOutId).GetTargetStr();

        if (addModuleNode) {
            Y_ASSERT(finalTargetCmd);
            fileNodeType = EMNT_NonParsedFile;
            moduleType = mainOutType;

            static constexpr TStringBuf modulePrefix = "$L/MODULE/"sv;
            ui32 moduleId = fileConf.Add(TString::Join(modulePrefix, mainOutName));
            moduleNode = &inputNode.AddOutput(moduleId, mainOutType);
        }

        if (mainOutAsExtra) {
            Y_ASSERT(IsFileType(fileNodeType));

            // Это пока очень временный способ пометить специальный узел, в котором будут общие свойства команды.
            static constexpr TStringBuf actionPrefix = "$L/ACTION/"sv;
            ui32 actionId = fileConf.Add(TString::Join(actionPrefix, mainOutName));

            TAddDepAdaptor& actionNode = inputNode.AddOutput(actionId, EMNT_NonParsedFile, !finalTargetCmd);
            TAddDepAdaptor& mainOutNode = inputNode.AddOutput(mainOutId, fileNodeType, !finalTargetCmd);

            return std::make_pair(std::ref(actionNode), std::ref(mainOutNode));
        } else {

            TAddDepAdaptor& mainOutNode = inputNode.AddOutput(mainOutId, fileNodeType, !finalTargetCmd);
            return std::make_pair(std::ref(mainOutNode), std::ref(mainOutNode));
        }
    };

    auto [actionNode, mainOutNode] = makeMainNodes();

    TVector<std::pair<std::reference_wrapper<TAddDepAdaptor>, TVarStrEx*>> outs;
    outs.push_back({mainOutNode, MainOutput});

    // 1. Inputs
    const ui64 groupId = Graph->Names().AddName(EMNT_Property, NStaticConf::INPUTS_MARKER);
    for (auto& input : GetInput()) {
        YDIAG(DG) << "Input dep: " << input.Name << Endl;

        if (input.IsGlob) {
            if (modBuilder.CurrentInputGroup != groupId) {
                inputNode.AddDepIface(EDT_Group, EMNT_Property, groupId);
                modBuilder.CurrentInputGroup = groupId;
            }
            ProcessGlobInput(actionNode, input.Name);
            continue;
        }

        if (finalTargetCmd) {
            modBuilder.AddDep(input, inputNode, true, groupId);
        } else {
            EMakeNodeType nodeType = EMNT_File;
            if (input.IsMacro) {
                nodeType = EMNT_UnknownCommand;
            } else if (input.IsDir) {
                nodeType = EMNT_MissingDir;
            } else if (input.NotFound) {
                nodeType = EMNT_MissingFile;
            } else if (input.IsOutputFile) {
                nodeType = EMNT_NonParsedFile;
            }

            if (nodeType == EMNT_NonParsedFile && TFileConf::IsLink(input.ElemId)) {
                ui32 targetId = TFileConf::GetTargetId(input.ElemId);
                auto inputNode = Graph->GetFileNodeById(targetId);
                if (inputNode.IsValid() && IsModuleType(inputNode->NodeType)) {
                    input.Name = NPath::GetTargetFromLink(input.Name);
                    input.ElemId = targetId;
                    nodeType = inputNode->NodeType;
                }
            }
            actionNode.AddDepIface(EDT_BuildFrom, nodeType, input.ElemId);
        }
        if (TFileConf::IsLink(input.ElemId) && NPath::GetType(NPath::ResolveLink(input.Name)) == NPath::ERoot::Build) {
            UpdIter->DelayedSearchDirDeps.GetDepsByType(EDT_Include)[MakeDepsCacheId(EMNT_NonParsedFile, input.ElemId)].Push(TFileConf::GetTargetId(input.ElemId));
        }
    }

    THashSet<TPropertyType> inducedDepsToUse;
    inducedDepsToUse.insert(TPropertyType{Graph->Names(), EVI_InducedDeps, "*"});

    // 2. Additional output files (we have to add them after the inputs or Induced deps processing will fail)
    // NOTE: this was previously after BuildCommand!
    if (hasExtraOuts) {
        YDIAG(V) << "For " << inputNodeName << " deps.size = " << GetOutput().size() - startCountOuts << "\n";
        for (auto& out : GetOutput()) {
            if (out.ElemId == mainOutId) {
                continue;
            }

            TAddDepAdaptor& extraOutNode = inputNode.AddOutput(out.ElemId, EMNT_NonParsedFile);
            outs.push_back({extraOutNode, &out});

            out.IsGlobal = out.IsGlobal || HasGlobalInput;
        }
    }

    bool mainOut = true;

    for (auto [outNodeRef, outVar] : outs) {
        TAddDepAdaptor& outNode = outNodeRef;

        if (mainOutAsExtra || !mainOut) {
            YDIAG(Star) << "Linking main " << actionNode.ElemId << " <-> " << outNode.ElemId << Endl;
            outNode.SetAction(&actionNode);
            outNode.AddDepIface(EDT_OutTogether, actionNode.NodeType, actionNode.ElemId);
            actionNode.AddDepIface(EDT_OutTogetherBack, outNode.NodeType, outNode.ElemId);
        }

        // Current implementation sets "pass induced" flags only for main output.
        // It is considered bug, but correct behaviour should be enabled only after additional testing.
        static constexpr bool oldPassMode = true;
        const bool setPassFlags = oldPassMode ? mainOut : true;

        // outVar is nullptr for "finalTargetCmd", which means this is a module target.
        // And we do not pass induced dependencies through modules.
        if (outVar) {
            const TIndDepsRule* rule = outNode.SetDepsRuleByName(outVar->Name);
            if (rule) {
                rule->InsertUseActionsTo(inducedDepsToUse);
            }

            if (setPassFlags) {
                auto setFlags = [&](ui32 elemId) {
                    TNodeData& nodeData = Graph->GetFileNodeData(elemId);
                    nodeData.PassNoInducedDeps = rule ? rule->PassNoInducedDeps : false;
                    nodeData.PassInducedIncludesThroughFiles = rule ? rule->PassInducedIncludesThroughFiles : false;
                };

                setFlags(outNode.ElemId);

                if (mainOut && mainOutAsExtra) {
                    setFlags(actionNode.ElemId);
                }
            }
        }

        mainOut = false;
    }

    if (moduleNode) {
        actionNode.AddDepIface(EDT_OutTogether, moduleNode->NodeType, moduleNode->ElemId);
        moduleNode->AddDepIface(EDT_OutTogetherBack, actionNode.NodeType, actionNode.ElemId);
    }

    if (finalTargetCmd) {
        for (const auto id : ownEntries) {
            Y_ENSURE(UpdIter != nullptr);
            auto modInfo = UpdIter->GetAddedModuleInfo(MakeDepsCacheId(EMNT_NonParsedFile, id));
            Y_ASSERT(modInfo != nullptr);
            if (modInfo != nullptr && modInfo->AdditionalOutput) {
                actionNode.AddDepIface(EDT_OutTogether, EMNT_NonParsedFile, id);
            }
        }
    }

    // OUTPUT_INCLUDES of main output must appear after OutTogetherBack edges
    for (auto [outNodeRef, _] : outs) {
        addOutputIncludes(outNodeRef);
    }

    for (const auto& out : GetOutput()) {
        UpdIter->MainOutputId[out.ElemId] = mainOutId;
    }
    UpdIter->PropsToUse[mainOutId] = std::move(inducedDepsToUse);

    // 4. The command
    YDIAG(DG) << "Cmd dep: " << curCmdName << " " << Cmd.Id << Endl;
    actionNode.AddDepIface(EDT_BuildCommand, EMNT_BuildCommand, cmdElemId);

    // 5. Imported variables (relevant for "structured" commands only)

    // note that we add all the vars _after_ the command,
    // so that graph walkers would be able to detect the command version
    // before getting to var processing (see TJSONEntryStats::StructCmdDetected)
    if (LocalVars) {
        TVector<TStringBuf> names;
        names.reserve(LocalVars->size());
        for (auto& var : *LocalVars)
            names.push_back(var.first);
        Sort(names);
        for (auto name : names) {
            auto storedInProperties = name.ends_with("__LATEOUT__");
            if (storedInProperties)
                continue;
            auto& var = LocalVars->at(name);
            auto varElemId = InitCmdNode(var);
            actionNode.AddUniqueDep(EDT_BuildCommand, EMNT_BuildVariable, varElemId);
        }
    }
    if (TBuildConfiguration::Workaround_AddGlobalVarsToFileNodes) {
        if (GlobalVars) {
            TVector<TStringBuf> names;
            names.reserve(GlobalVars->size());
            for (auto& var : *GlobalVars)
                names.push_back(var.first);
            Sort(names);
            for (auto name : names) {
                auto& var = GlobalVars->at(name);
                auto varElemId = InitCmdNode(var);
                actionNode.AddUniqueDep(EDT_Include, EMNT_BuildCommand, varElemId);
            }
        }
    }

    return true;
}

void TCommandInfo::ProcessGlobInput(TAddDepAdaptor& node, TStringBuf globStr) {

    auto CreateGlobNode = [&node, this](const TModuleGlobInfo& globInfo) {
        node.AddUniqueDep(EDT_BuildFrom, EMNT_BuildCommand, globInfo.GlobId);
        auto& [id, entryStats] = *UpdIter->Nodes.Insert(MakeDepsCacheId(EMNT_BuildCommand, globInfo.GlobId), &(UpdIter->YMake), Module);
        auto& globNode = entryStats.GetAddCtx(Module, UpdIter->YMake);
        globNode.NodeType = EMNT_BuildCommand;
        globNode.ElemId = globInfo.GlobId;
        entryStats.SetOnceEntered(false);
        entryStats.SetReassemble(true);
        PopulateGlobNode(globNode, globInfo);
    };

    try {
        TExcludeMatcher excludeMatcher;
        TUniqVector<ui32> matches;
        TGlob glob(Graph->Names().FileConf, globStr, Module->GetDir());
        for (const auto& result : glob.Apply(excludeMatcher)) {
            matches.Push(Graph->Names().FileConf.ConstructLink(ELinkType::ELT_Text, result).GetElemId());
        }
        const TString globCmd = FormatCmd(Module->GetName().GetElemId(), NProps::LATE_GLOB, globStr);
        TModuleGlobInfo globInfo = {
            Graph->Names().AddName(EMNT_BuildCommand, globCmd),
            Graph->Names().AddName(EMNT_Property, FormatProperty(NProps::GLOB_HASH, glob.GetMatchesHash())),
            glob.GetWatchDirs().Data(),
            matches.Take(),
            {},
            0
        };
        CreateGlobNode(globInfo);
    } catch (const yexception& error){
        YConfErr(Syntax) << "Invalid pattern in [[alt1]]" << globStr << "[[rst]]: " << error.what() << Endl;
    }
}

bool TCommandInfo::ProcessVar(TModuleBuilder& modBuilder, TAddDepAdaptor& inputNode) {
    Y_ENSURE(UpdIter != nullptr);
    SBDIAG << "Process command: " << Get1(&Cmd) << Endl;

    if (!Cmd) {
        return true;
    }

    // 1. Inputs
    for (auto& input : GetInput()) {
        modBuilder.AddInputVarDep(input, inputNode);

        if (TFileConf::IsLink(input.ElemId) && NPath::GetType(NPath::ResolveLink(input.Name)) == NPath::ERoot::Build) {
            UpdIter->DelayedSearchDirDeps.GetDepsByType(EDT_Include)[MakeDepsCacheId(EMNT_NonParsedFile, input.ElemId)].Push(TFileConf::GetTargetId(input.ElemId));
        }
    }

    return true;
}

void TCommandInfo::AddCfgVars(const TVector<TDepsCacheId>& varLists, ui64 nsId, TNodeAddCtx& dst, bool structCmd) {
    TStringStream cfgVars;
    for (auto id : varLists) { // this loop is not optimized because there's hardly 1 element in varLists
        TStringBuf name = Graph->GetCmdNameByCacheId(id).GetStr();
        TVector<TStringBuf> vars;
        Split(GetPropertyValue(name), " ", vars);
        for (auto var : vars) {
            cfgVars << TStringBuf(" ") << var << TStringBuf("=$") << var;
        }
    }

    //Attach CFG_VARS to the source file
    YDIAG(VV) << "CFG_VARS [" << Module->Vars.Id << "] -> " << cfgVars.Str() << Endl;
    TYVar var;

    if (structCmd) {
        auto compiled = UpdIter->YMake.Commands.Compile(cfgVars.Str(), *Conf, Module->Vars, false, {});
        auto subExpr = UpdIter->YMake.Commands.Add(*Graph, std::move(compiled.Expression));
        auto subExprRef = Graph->Names().CmdNameById(subExpr).GetStr();
        auto subBinding = TYVar();
        subBinding.SetSingleVal("CFG_VARS", subExprRef, 0);
        auto cmdElemId = InitCmdNode(subBinding);
        dst.AddDep(EDT_BuildCommand, EMNT_BuildVariable, cmdElemId);
    } else {
        TVars vars(&Module->Vars); // TODO: move it to TCommandInfo?
        var.SetSingleVal("CFG_VARS", cfgVars.Str(), nsId, Module->Vars.Id);
        vars.Id = var.Id;
        ui64 id = InitCmdNode(var);
        dst.AddDep(EDT_Include, EMNT_BuildCommand, id);
        FillAddCtx(var, vars);
    }
}

void TCommandInfo::FillCoords(
    const TYVar* origin,
    TVector<TMacroData>& macros,
    ESubstMode substMode,
    const TVars& localVars,
    ECmdFormat cmdFormat,
    bool setAddCtxFilled
) {
    TVector<TMacroData> lmacros;
    TDbgPadHelper MsgPad(MsgDepth, ::MsgPad);
    SBDIAG << "FM " << Get1(&Cmd) << "\n";
    // fill more inputs
    for (auto& macroData : macros) {
        SBDIAG << "FM: " << macroData.Name << "(o=" << macroData.Flags.Get(EMF_Output) << ",t=" << macroData.Flags.Get(EMF_Tmp)
               << ",i=" << macroData.Flags.Get(EMF_Input) << ",t=" << macroData.Flags.Get(EMF_Tool) << ",oi="
               << macroData.Flags.Get(EMF_OutputInclude) << ",a=" << macroData.HasArgs << ",A=" << macroData.HasOwnArgs
               << ",D=" << macroData.Flags.Get(EMF_WorkDir)
               << ")\n";

        const TYVar* macroVar = nullptr;
        if (macroData.HasArgs) {
            macroVar = GetSpecMacroVar(origin, macroData.Name, macroData.Args(), localVars);
        }
        if (macroVar == nullptr) {
            macroVar = (macroData.SameName && origin) ? origin->BaseVal : (macroData.RawString ? nullptr : localVars.Lookup(macroData.Name));
        }
        SBDIAG << "FMJ: " << macroData.SameName << !!macroVar << (!macroVar || macroVar->DontExpand) << Endl;
        if (macroData.SameName && origin && !macroVar && cmdFormat != ECF_Json) {
            if (origin->Id) {
                ythrow TError() << macroData.Name << " in " << Get1(origin) << " was set with recurse but not defined on base level of config." << Endl;
            } else {
                ythrow TError() << macroData.Name << " in " << Get1(origin) << " was declared with recurse in ymake.conf." << Endl;
            }
        }

        if (origin && GetAddCtx(*origin) && (IsGlobalReservedVar(macroData.Name) || (macroVar && macroVar->IsReservedName))) {
            // Track used reserved vars to substitute them properly
            GetAddCtx(*origin)->AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty(NProps::USED_RESERVED_VAR, macroData.Name));
        }

        if (origin && GetAddCtx(*origin) && macroData.Flags.Get(EMF_LateOut)) {
            if (!IsInternalReservedVar(macroData.Name)) {
                YConfErr(MacroUse) << "Can't process var " << macroData.Name << " as late out! Use only late vars." << Endl;
                continue;
            }
            if (!macroData.Flags.Get(EMF_Hide)) {
                YConfErr(MacroUse) << "Late out expression " << macroData.OrigFragment << " should have hide modifier flag." << Endl;
                continue;
            }
            GetAddCtx(*origin)->AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty(NProps::LATE_OUT, macroData.OrigFragment));
        }

        if ((!macroVar || macroVar->DontExpand) && !macroData.RawString) {
            continue;
        }

        if (origin && macroVar && !macroData.IsOwnArg && !macroVar->empty() && (*macroVar)[0].HasPrefix /* TODO: !.IsFile ? */ && !IsInternalReservedVar(macroData.Name)) {
            Y_ASSERT(macroVar->size() == 1); // not implemented yet
            if (GetAddCtx(Cmd)) { // as a flag of graph construction mode
                Y_ASSERT(GetId(Get1(macroVar)) == macroVar->Id);
                const ui64 macroVarElemId = InitCmdNode(*macroVar);
                const TYVar* addMacroDataTo = origin;
                if (macroVar->GenFromFile || localVars.IsIdDeeper(origin->Id, macroVar->Id)) {
                    addMacroDataTo = &Cmd;
                    if (!macroVar->GenFromFile) {
                        const TVars* baseVars = localVars.GetBase(origin->Id);
                        const TYVar* macroVarFromBaseVars = baseVars->Lookup(macroData.Name);
                        if (macroVarFromBaseVars && !origin->AddCtxFilled) {
                            const ui64 macroVarFromBaseVarsElemId = InitCmdNode(*macroVarFromBaseVars);
                            GetAddCtx(*origin)->AddUniqueDep(EDT_Include, EMNT_BuildCommand, macroVarFromBaseVarsElemId);

                            if (!macroVarFromBaseVars->AddCtxFilled) {
                                FillAddCtx(*macroVarFromBaseVars, *baseVars);
                            }
                        }
                    }
                }

                if (!addMacroDataTo->AddCtxFilled) {
                    GetAddCtx(*addMacroDataTo)->AddUniqueDep(EDT_Include, EMNT_BuildCommand, macroVarElemId);
                }
            }
        }

        if (macroData.Flags.Get(EMF_Tool) && !macroData.IsOwnArg && origin && GetAddCtx(*origin) && !origin->AddCtxFilled && GetAddCtx(Cmd)) {
            const TString toolValue = GetToolValue(macroData, localVars);
            if (toolValue.empty()) {
                continue;
            }
            const TString tool = NPath::ConstructPath(toolValue, NPath::Source);
            SBDIAG << "Tool dep: " << tool << Endl;
            GetAddCtx(*origin)->AddUniqueDep(EDT_Include, EMNT_Directory, tool.data());
            continue;
        }

        if (macroVar && (macroData.HasArgs || macroData.HasOwnArgs)) {
            if (macroData.NeedCoord()) {
                YConfWarn(MacroUse) << "Macro with arguments can't be input/output/tool: " << macroData.OrigFragment << Endl;
            }

            SBDIAG << "OA: " << macroData.Name << " -> " << Get1(macroVar) << "\n";
            TVarStr val = (*macroVar)[0];
            ApplyMods(macroVar, val, origin, macroData, substMode, localVars, cmdFormat);
            continue;
        }

        for (size_t cnt = 0, sz = macroData.RawString ? 1 : macroVar->size(); cnt < sz; cnt++) {
            // inInp? NPath::ConstructPath(NPath::CutType(var[n]), NPath::Build) : var[n]
            TVarStr val = macroData.RawString ? TVarStr(macroData.Name) : (*macroVar)[cnt];
            if (val.HasPrefix) {
                SBDIAG << "FM.HasPrefix: " << val.Name << ", " << macroVar->Id << Endl;
            }

            // note: same condition in ApplyMods (still check for CommandSeparator if you remove this limitation)
            if (!macroData.RawString || macroData.Flags.Any({EMF_SetEnv, EMF_WorkDir})) {
                lmacros.clear();
                GetMacrosFromPattern(val.Name, lmacros, val.HasPrefix);
                if (!lmacros.empty()) {
                    FillCoords((macroData.RawString ? origin : macroVar), lmacros, substMode, localVars, cmdFormat, !macroData.RawString);
                }
            }

            if (val.HasPrefix) {
                val.Name = GetCmdValue(val.Name);
                val.HasPrefix = false;
            }

            val.NeedSubst = false;
            if (macroData.NeedCoord()) {
                ApplyMods(macroVar, val, origin, macroData, substMode, localVars, cmdFormat);
            }
        }

        macroData.CoordsFilled = true;
    }

    if (setAddCtxFilled && origin && GetAddCtx(Cmd) && origin != &Cmd) {
        origin->AddCtxFilled = true;
    }
}

bool TCommandInfo::IsIncludeVar(const TStringBuf& cur) {
    return cur.StartsWith(TModuleIncDirs::VAR_PREFIX) && cur.EndsWith(TModuleIncDirs::VAR_SUFFIX);
}

bool TCommandInfo::IsReservedVar(const TStringBuf& cur, const TVars& vars) {
    return IsInternalReservedVar(cur) || vars.IsReservedName(cur);
}

bool TCommandInfo::IsGlobalReservedVar(const TStringBuf& cur, const TVars& vars) {
    using namespace NVariableDefs;
    return vars.IsReservedName(cur) || NYMake::IsGlobalResource(cur) || IsIncludeVar(cur) ||
        EqualToOneOf(cur, VAR_MANAGED_PEERS_CLOSURE, VAR_MANAGED_PEERS, VAR_APPLIED_EXCLUDES);
}

bool TCommandInfo::IsReservedVar(const TStringBuf& cur) const {
    return IsReservedVar(cur, Conf->CommandConf);
}

bool TCommandInfo::IsGlobalReservedVar(const TStringBuf& cur) const {
    return IsGlobalReservedVar(cur, Conf->CommandConf);
}

TString TCommandInfo::GetToolValue(const TMacroData& macroData, const TVars& vars) {
    Y_ASSERT(macroData.Flags.Get(EMF_Tool));
    if (macroData.RawString) {
        return ToString(macroData.Name);
    }
    return SubstVarDeeply(macroData.Name, vars);
}

const TYVar* TCommandInfo::GetSpecMacroVar(const TYVar* origin, const TStringBuf& genericMacroName, const TStringBuf& args, const TVars& vars) {
    const TYVar* specMacroVar = nullptr;
    const auto it = Conf->BlockData.find(genericMacroName);
    if (it != Conf->BlockData.end()) {
        const auto& data = it->second;
        if (Conf->RenderSemantics && UpdIter && !data.HasSemantics) {
            YConfErr(NoSem) << "No semantics specified for macro " << it->first << " in module " << Module->GetUserType() << ". It is not intended for export." << Endl;
        }
        if (data.IsGenericMacro) {
            // Try to compute macro specialization:
            // 1) try to compute the actual args for this macro call
            // 2) try to find corresponding specialization of macro by the first actual arguments (temporary
            //    restriction is to take into account only the first actaul argument)
            auto substArgs = SubstMacroDeeply(origin, args, vars);
            TVector<TStringBuf> actualArgs;
            if (!SplitArgs(substArgs, actualArgs)) {
                ythrow yexception() << "Expected argument list in () braces, but got [" << args << "]";
            }
            auto specMacroName = Conf->GetSpecMacroName(genericMacroName, actualArgs);
            specMacroVar = vars.Lookup(specMacroName);
        }
    }
    return specMacroVar;
}

void TCommandInfo::FillAddCtx(const TYVar& var, const TVars& parentVars) {
    // from GeneralParser::ProcessBuildCommand
    TVector<TMacroData> tokens;
    GetMacrosFromPattern(Get1(&var), tokens, true);
    if (tokens.empty()) {
        return;
    }

    const TVars& vars = *parentVars.GetBase(var.Id);
    for (const auto& token : tokens) {
        const TStringBuf tokenName = token.Name;
        SBDIAG << "FillAddCtx " << Get1(&var) << " -> " << tokenName << "\n";
        if (IsInternalReservedVar(tokenName)) {
            continue;
        }

        if (IsGlobalReservedVar(tokenName)) {
            GetAddCtx(var)->AddUniqueDep(EDT_Property, EMNT_Property, FormatProperty(NProps::USED_RESERVED_VAR, tokenName));
        }

        if (token.Flags.Get(EMF_Tool) && !token.IsOwnArg) {
            const TString toolValue = GetToolValue(token, parentVars);
            if (toolValue.empty()) {
                continue;
            }
            const TString tool = NPath::ConstructPath(toolValue, NPath::Source);
            SBDIAG << "Tool dep: " << tool << Endl;
            GetAddCtx(var)->AddUniqueDep(EDT_Include, EMNT_Directory, tool.data());
            continue;
        }

        if (token.RawString || token.IsOwnArg) {
            continue;
        }

        const TYVar* cmd = token.RawString ? nullptr : vars.Lookup(tokenName);
        if (cmd && token.SameName) {
            cmd = var.BaseVal;
        }

        if (cmd && cmd->GenFromFile) {
            if (var.Id && !Conf->NoParseSrc) {
                YConfWarn(UndefVar) << GetCmdName(Get1(&var)) << ": command dep " << tokenName << " must be redefined\n";
            }
            continue;
        }

        //YDIAG(Dev) << "cur = " << cur << " " << token.RawString << ", " << token.IsOwnArg << ", " << token.IsTool << "\n";

        if (!cmd) {
            if (!IsGlobalReservedVar(tokenName)) {
                YConfWarn(UndefVar) << GetCmdName(Get1(&var)) << ": command dep " << tokenName << " not found\n";
                YConfWarn(UndefVar) << "> HERE: " << Get1(&var) << Endl;
                GetAddCtx(var)->AddDep(EDT_Include, EMNT_UnknownCommand, tokenName);
            }
        } else {
            const TStringBuf cmdStr = Get1(cmd);
            const ui64 cmdElemId = InitCmdNode(*cmd);
            if (GetAddCtx(var)->AddUniqueDep(EDT_Include, EMNT_BuildCommand, cmdElemId)) {
                SBDIAG << "Command dep: " << cmdStr << Endl;
                if (!cmd->AddCtxFilled) {
                    FillAddCtx(*cmd, vars);
                }
            }
        }
    }

    if (GetAddCtx(Cmd)) {
        var.AddCtxFilled = true;
    }
}

static void PostProcessRawMacroValue(ECmdFormat cmdFormat, const TMacroData& macroData, TVarStr& value) {
    // There is currently nothing to do for cmdFormat other than ECF_ExpandSimpleVars
    if (cmdFormat == ECF_ExpandSimpleVars) {
        // cmdFormat == ECF_ExpandSimpleVars means that we are currently in a some kind of preprocessing mode
        // when command is rendered for JSon format. This means that the real filling in of additional info
        // such as 'cwd', 'environment' will be processed later, so we need preserve this info for further
        // processing. But since the context of substitution is totally lost (for example, the names and values
        // of formal arguments of macros called during substitution/evaluation at this preprocessing stage)
        // we are substituting raw values with corresponding macro modificators
        TString modifiers;
        for (auto flag : { EMF_WorkDir, EMF_AsStdout, EMF_SetEnv, EMF_Quote, EMF_BreakQuote, EMF_QuoteEach, EMF_Comma, EMF_ResourceUri, EMF_TaredOut } ) {
            if (macroData.Flags.Get(flag)) {
                ( modifiers += ';' ) += ModifierFlagToName(flag);
            }
        }
        if (!modifiers.empty()) {
            value.Name = TString::Join("${", modifiers.substr(1), ":\"", value.Name, "\"}");
        }
    }
}

bool TCommandInfo::ApplyMods(const TYVar* valVar, TVarStr& value, const TYVar* modsVar, const TMacroData& macroData, ESubstMode substMode, const TVars& parentVars, ECmdFormat cmdFormat) {
    if (macroData.Flags.Get(EMF_Comma)) {
        value.Name = TStringBuf(",");
        return true;
    }
    if (macroData.Flags.Get(EMF_KeyValue)) {
        TString kvValue = SubstMacroDeeply(nullptr, value.Name, parentVars, false);
        TStringBuf name(kvValue);
        TStringBuf before;
        TStringBuf after;

        if (name.TrySplit(' ', before, after)) {
            TString val = TString{after};
            if (cmdFormat == ECF_Json) {
                BreakQuotedExec(val, "\", \"", true);
            }
            GetOrInit(KV)[before] = val;
        } else {
            GetOrInit(KV)[name] = "yes";
        }

        return false;
    }
    if (macroData.Flags.Get(EMF_Requirements)) {
        if (!Requirements) {
            if (Conf) {
                Requirements = MakeHolder<THashMap<TString, TString>>(Conf->GetDefaultRequirements());
            } else {
                Requirements = MakeHolder<THashMap<TString, TString>>();
            }
        }
        if (value.NeedSubst) {
            TStringBuf modtoken = value.Name;
            if (value.HasPrefix) {
                modtoken = GetCmdValue(value.Name);
            }
            ParseRequirements(SubstMacroDeeply(valVar, modtoken, parentVars, false), *Requirements);
        } else {
            ParseRequirements(value.Name, *Requirements);
        }
        return false;
    }
    if (macroData.Flags.Get(EMF_Unknown)) {
        YConfWarn(MacroUse) << "Bad modifier in config for: " << value.Name << "; skip it." << Endl;
    }
    TStringBuf modtoken = value.Name; //modified subst
    if (value.HasPrefix)
        modtoken = GetCmdValue(modtoken);
    SBDIAG << "ApplyMods[" << parentVars.Id << "]: " << macroData.Name << " -> " << value.Name << Endl;

    if (macroData.HasArgs || macroData.HasOwnArgs) {
        SBDIAG << "ApplyMods: B" << Endl;
        value.Name = MacroCall(valVar, modtoken, modsVar, macroData.Args(), substMode, parentVars, cmdFormat, modsVar != nullptr);

        SBDIAG << "ApplyMods: C: " << value.Name << Endl;
        return true; // all other macroData ignored
    }

    if (!macroData.RawString && (value.NeedSubst || AllVarsNeedSubst)) { // note: by construction, these are all non-local vars from CommandMining
        SBDIAG << "ApplyMods: NeedSubst" << Endl;
        modtoken = value.Name = SubstMacro(valVar, value.Name, substMode, parentVars, cmdFormat, value.HasPrefix, macroData.GetFormatFor());
    }

    if (value.HasPeerDirTags) {
        // 'HasPeerdirTags' flag may be set up only for PEERS internal variable and
        // it means that PEERDIR path may be prefixed with comma separated list of tags
        // example: ",final,GO$(BUILD_ROOT)/contrib/go/_std_1.11.4/src/internal/cpu/cpu.a"
        // The prefix must be cut off if the PEERDIR path satisfies conditions of TagsIn
        // and TagsOut filters
        auto delim = modtoken.find("$");
        if (delim != TStringBuf::npos) {
            if (!macroData.TagsIn.empty() || !macroData.TagsOut.empty()) {
                TVector<TStringBuf> tags = StringSplitter(TStringBuf(modtoken.data(), delim)).Split(',').SkipEmpty();
                if (!macroData.TagsIn.empty()) {
                    if (!MatchTags(macroData.TagsIn, tags)) {
                        return false;
                    }
                }
                if (!macroData.TagsOut.empty()) {
                    if (MatchTags(macroData.TagsOut, tags)) {
                        return false;
                    }
                }
            }
            modtoken = modtoken.substr(delim);
        }
    }

    if (macroData.ExtFilter.size() && !value.Name.EndsWith(macroData.ExtFilter))
        return false;

    if (macroData.SkipByExtFilter.size() && value.Name.EndsWith(macroData.SkipByExtFilter)) {
        return false;
    }

    if (macroData.Flags.Get(EMF_CutExt) || macroData.Flags.Get(EMF_CutAllExt)) {    //todo: gperf
        size_t slash = modtoken.rfind(NPath::PATH_SEP); //todo: windows slash!
        if (slash == TString::npos)
            slash = 0;
        size_t dot = macroData.Flags.Get(EMF_CutAllExt) ? modtoken.find('.', slash) : modtoken.rfind('.');
        if (dot != TString::npos && dot >= slash)
            modtoken = modtoken.substr(0, dot);
    }

    if (macroData.Flags.Get(EMF_LastExt)) {
        // It would be nice to use some common utility function from common/npath.h,
        // but Extension function implements rather strange behaviour
        auto slash = modtoken.rfind(NPath::PATH_SEP);
        auto dot = modtoken.rfind('.');
        if (dot != TStringBuf::npos && (slash == TStringBuf::npos || slash < dot)) {
            modtoken = modtoken.SubStr(dot + 1);
        } else {
            modtoken.Clear();
        }
    }

    if (macroData.Flags.Get(EMF_CutPath)) {
        size_t slash = modtoken.rfind(NPath::PATH_SEP);
        if (slash != TString::npos)
            modtoken = modtoken.substr(slash + 1);
    }

    bool addSuffix = true;
    if (macroData.Flags.Get(EMF_HasDefaultExt)) {
        size_t dot = modtoken.rfind('.');
        size_t slash = modtoken.rfind(NPath::PATH_SEP);
        bool hasSpecExt = slash != TString::npos ? (dot > slash) : true;
        if (dot != TString::npos && hasSpecExt)
            addSuffix = false;
    }

    TString prefix = SubstMacroDeeply(nullptr, macroData.Prefix, parentVars, false);
    TString ns = SubstMacroDeeply(nullptr, macroData.Namespace, parentVars, false);
    TString suffix = addSuffix && macroData.Suffix.size() ? SubstMacroDeeply(nullptr, macroData.Suffix, parentVars, false): "";
    if (!ns.empty() && !prefix.empty()) {
        YConfWarn(BadMacro) << "Don't use to_namespace and prefix modifiers together. The prefix modifier will be selected." << Endl;
    }
    if (!ns.empty() && prefix.empty()) {
        value.Name = ApplyNamespaceModifier(Conf->BuildRoot.c_str(), ns, modtoken, suffix);
    } else {
        value.Name = TString::Join(prefix, modtoken, suffix);
    }

    if (macroData.Flags.Get(EMF_ToUpper)) {
        value.Name.to_upper();
    }
    if (macroData.Flags.Get(EMF_ToLower)) {
        value.Name.to_lower();
    }

    if (macroData.Context) {
        value.Name = TFileConf::ConstructLink(macroData.Context, NPath::ConstructPath(value.Name));
    }

    if (macroData.NeedCoord() && !value.Name.empty()) {
        TSpecFileList* to = nullptr;
        if (macroData.Flags.Get(EMF_Input)) {
            if (value.IsAuto) {
                to = &GetAutoInputInternal();
            } else {
                to = &GetInputInternal();
            }
        } else if (macroData.Flags.Get(EMF_Output) || macroData.Flags.Get(EMF_Tmp)) {
            to = &GetOutputInternal();
        } else if (macroData.Flags.Get(EMF_Tool)) {
            to = &GetToolsInternal();
        } else if (macroData.Flags.Get(EMF_Result)) {
            to = &GetResultsInternal();
        } else if (macroData.Flags.Get(EMF_InducedDeps)) {
            TString ext = SubstMacroDeeply(nullptr, macroData.InducedDepsExt, parentVars, false);
            to = &GetOutputIncludeForTypeInternal(ext);
        } else {
            to = &GetOutputIncludeInternal();
        }
/* FIXME(dimdim11) Replace block below by commented code fail many tests
        Using Update after Push is bad idea, THash<TVarStr> using AllFlags,
        but Update does not update HashMap, only update item at position without
        patch THashMap, which check unique items

        TVarStr var(value);
        var.NoAutoSrc |= macroData.Flags.Get(EMF_NoAutoSrc);
        var.IsTmp |= macroData.Flags.Get(EMF_Tmp);
        var.ResolveToBinDir |= macroData.Flags.Get(EMF_ResolveToBinDir);
        var.NoTransformRelativeBuildDir |= macroData.Flags.Get(EMF_NoTransformRelativeBuildDir);
        var.FromLocalVar |= macroData.IsOwnArg;
        var.AddToIncl |= macroData.Flags.Get(EMF_AddToIncl);
        var.NoRel |= macroData.Flags.Get(EMF_NoRel);
        var.DirAllowed |= macroData.Flags.Get(EMF_DirAllowed);
        var.RawInput |= macroData.Flags.Get(EMF_Input) && macroData.RawString;
        var.AddToModOutputs |= macroData.Flags.Get(EMF_AddToModOutputs);
        var.Result |= macroData.Flags.Get(EMF_Result);
        var.IsOutput |= macroData.Flags.Get(EMF_Output);
        var.Main |= macroData.Flags.Get(EMF_Main);
        var.IsGlobal |= macroData.Flags.Get(EMF_Global);
        var.ResolveToModuleBinDirLocalized |= macroData.Flags.Get(EMF_ModLocal);
        var.OutInclsFromInput |= macroData.Flags.Get(EMF_OutInclsFromInput);
        const auto [id, _] = to->Push(std::move(var));
*/
        const auto [id, _] = to->Push(value);
        to->Update(id, [&macroData](auto& var) {
            var.NoAutoSrc |= macroData.Flags.Get(EMF_NoAutoSrc);
            var.IsTmp |= macroData.Flags.Get(EMF_Tmp);
            var.ResolveToBinDir |= macroData.Flags.Get(EMF_ResolveToBinDir);
            var.NoTransformRelativeBuildDir |= macroData.Flags.Get(EMF_NoTransformRelativeBuildDir);
            var.FromLocalVar |= macroData.IsOwnArg;
            var.AddToIncl |= macroData.Flags.Get(EMF_AddToIncl);
            var.NoRel |= macroData.Flags.Get(EMF_NoRel);
            var.DirAllowed |= macroData.Flags.Get(EMF_DirAllowed);
            var.RawInput |= macroData.Flags.Get(EMF_Input) && macroData.RawString;
            var.AddToModOutputs |= macroData.Flags.Get(EMF_AddToModOutputs);
            var.Result |= macroData.Flags.Get(EMF_Result);
            var.IsOutput |= macroData.Flags.Get(EMF_Output);
            var.Main |= macroData.Flags.Get(EMF_Main);
            var.IsGlobal |= macroData.Flags.Get(EMF_Global);
            var.ResolveToModuleBinDirLocalized |= macroData.Flags.Get(EMF_ModLocal);
            var.OutInclsFromInput |= macroData.Flags.Get(EMF_OutInclsFromInput);
        });

        Y_ASSERT(id < 32768);
        value.CurCoord = id;
    }

    return true;
}

bool TCommandInfo::SubstData(
    const TYVar* origin,
    TMacroData& macro,
    const TVars& vars,
    ECmdFormat cmdFormat,
    ESubstMode substMode,
    TString& result,
    ECmdFormat formatFor,
    const TSubstObserver& substObserver
) {
    TDbgPadHelper MsgPad(MsgDepth, ::MsgPad);
    if (EqualToOneOf(cmdFormat, ECF_ExpandVars, ECF_ExpandFoldableVars) && macro.RawString) {
        return false;
    }

    const TYVar* macroVar = nullptr;
    if (macro.HasArgs && Conf != nullptr) {
        macroVar = GetSpecMacroVar(origin, macro.Name, macro.Args(), vars);
    }
    if (macroVar == nullptr) {
        macroVar = (macro.SameName && !macro.HasOwnArgs && origin) ? origin->BaseVal // for SET_APPEND
                                                                   : vars.Lookup(macro.Name);
    }

    if (!macro.RawString && (!macroVar || macroVar->DontExpand)) {
        macroVar = nullptr;
    }

    SubstData(origin, macro, macroVar, vars, cmdFormat, substMode, result, formatFor, substObserver);
    return true;
}

void TCommandInfo::SubstData(
    const TYVar* origin,
    TMacroData& macro,
    const TYVar* pvar,
    const TVars& vars,
    ECmdFormat cmdFormat,
    ESubstMode substMode,
    TString& result,
    ECmdFormat formatFor,
    const TSubstObserver& substObserver
) {
    // Do not even try to expand any macro in ECF_ExpandFoldableVars mode if any
    // modifier set for it.
    if (cmdFormat == ECF_ExpandFoldableVars && !macro.Flags.Empty()) {
        result += macro.OrigFragment;
        return;
    }

    const TYVar* macroCoordVal = macro.NeedCoord() ? vars.Lookup(macro.CoordWhere()) : nullptr;
    bool hasPrev = false;
    size_t num = (macro.RawString || pvar == nullptr) ? 1 : (*pvar).size();
    SBDIAG << "SD IN " << macro.Name << ": " << num << Endl;

    //bool quoted = macro.Quoted || macro.GetFormatFor() == ECF_Json;
    bool breakQuoteNE = macro.GetFormatFor() == ECF_Json && !macro.Flags.Get(EMF_BreakQuote) && !macro.Quoted && !macro.Flags.Get(EMF_Quote);
    bool breakQuote = macro.Flags.Get(EMF_BreakQuote) || breakQuoteNE;
    bool quote = macro.Flags.Get(EMF_Quote) && !macro.Quoted && macro.GetFormatFor() != ECF_Json;
    if (EqualToOneOf(cmdFormat, ECF_ExpandSimpleVars, ECF_ExpandFoldableVars)) {
        breakQuoteNE = false;
        breakQuote = false;
        quote = false;
    }
    const TStringBuf quoteDelim = macro.GetFormatFor() == ECF_Json ? "\", \"" : "\" \"";

    decltype(MkCmdAcceptor) cmdAcceptor = (EqualToOneOf(cmdFormat, ECF_ExpandSimpleVars, ECF_ExpandVars, ECF_ExpandFoldableVars)) ? nullptr : MkCmdAcceptor;
    const size_t substDataOffset = result.size();

    if (quote)
        result += "\"";
    for (size_t cnt = 0; cnt < num; cnt++) {
        TVarStr nextsubst(macro.RawString ? TVarStr(macro.Name) : (pvar ? (*pvar)[cnt] : macro.OrigFragment));
        if (nextsubst.StructCmd) {
            Y_DEBUG_ABORT_UNLESS(CommandSource);
            if (Y_LIKELY(CommandSource)) {
                auto& cmdSrc = *CommandSource;
                auto& conf = Graph->Names().CommandConf;
                auto& expr = *cmdSrc.Get(nextsubst.Name, &conf);
                auto argses = TCommands::SimpleCommandSequenceWriter()
                    .Write(cmdSrc, expr, vars, {}, *this, &conf, *Conf)
                    .Extract();
                TVector<TString> cmds;
                cmds.reserve(argses.size());
                for (auto& args : argses) {
                    for (auto& arg : args)
                        arg = "\"" + EscapeC(arg) + "\"";
                    cmds.push_back(JoinSeq(' ', args));
                }
                nextsubst.Name = JoinSeq(" && ", cmds);
            }
        }
        nextsubst.HasPeerDirTags = pvar && (*pvar)[cnt].HasPeerDirTags;
        SBDIAG << "SD+++ " << nextsubst.Name << " [" << cnt << "] coord=" << (macro.NeedCoord() ? macro.CoordWhere() : "no") << "\n";
        if (!macro.RawString && pvar == nullptr) {
            // Do not apply modifiers if the value of variable is not set, just leave the macro variable unexpanded
        } else if (!ApplyMods(macro.RawString ? origin : pvar, nextsubst, origin, macro, substMode, vars, cmdFormat)) {
            continue;
        }
        if (macro.Flags.Get(EMF_Hide)) {
            continue;
        }
        if (pvar && !pvar->empty() && pvar->IsReservedName) {
            nextsubst = (*pvar)[cnt];
        } else if (macro.NeedCoord() && !nextsubst.Name.empty() && (substMode == ESM_DoSubst || substMode == ESM_DoBothCm)) {
                // If the macro is a tool or result, use the value set by MineVariables.
                if (macro.Flags.Get(EMF_Tool) || (macro.Flags.Get(EMF_Result) && !macro.Flags.Get(EMF_Output))) {
                    auto& minedVarsHolder = macro.Flags.Get(EMF_Tool) ? ToolPaths : ResultPaths;
                    auto& minedVars = GetOrInit(minedVarsHolder);
                    TString normalizedName = NPath::ConstructYDir(nextsubst.Name, TStringBuf(), ConstrYDirNoDiag);
                    auto key = NPath::CutType(normalizedName);
                    auto item = minedVars.find(key);
                    if (item != minedVars.end()) {
                        nextsubst = TVarStr(item->second);
                    } else if (macro.Flags.Get(EMF_Tool)) {
                        // when preparing macro arguments for the new command engine,
                        // we do not allocate a command node (yet), do not build a respective subgraph, and do no variable mining;
                        // instead, we reconstruct the tool reference to be processed by said engine later
                        // (in a manner not unlike the `PostProcessRawMacroValue()` thing below);

                        // a motivating example:
                        // ```
                        // M4_PATH=contrib/tools/m4
                        // M4_BINARY=${tool:M4_PATH}
                        // RUN_PROGRAM(contrib/tools/bison ... ENV M4=${M4_BINARY} ...)
                        // ```

                        nextsubst = TVarStr(TString::Join("${tool:\"", EscapeC(TString(key)), "\"}"));
                    } else {
                        throw TConfigurationError() << "Could not handle the result reference " << nextsubst.Name;
                    }
                } else if (macroCoordVal != nullptr) {
                    nextsubst = (*macroCoordVal)[nextsubst.CurCoord];
                }
        }
        if (nextsubst.IsPathResolved) {
            if (macro.Flags.Get(EMF_PrnRootRel)) { // for both coord and non-coord
                nextsubst.Name.assign(NPath::CutType(Conf->CanonPath(nextsubst.Name)));
            }
            if (macro.Flags.Get(EMF_PrnOnlyRoot)) {
                nextsubst.Name.assign(Conf->RealPathRoot(Conf->CanonPath(nextsubst.Name)).c_str());
            }
            if (macro.Flags.Get(EMF_WndBackSl)) {
                SubstGlobal(nextsubst.Name, NPath::PATH_SEP, TPathSplitTraitsWindows::MainPathSep);
            }
            if (macro.Flags.Get(EMF_QuoteEach) && cmdFormat != ECF_ExpandSimpleVars)
                nextsubst.Name = "\"" + nextsubst.Name + "\"";
        }

        if (macro.RawString || pvar) {
            PostProcessRawMacroValue(cmdFormat, macro, nextsubst);
        }

        if (breakQuote) {
            if (macro.GetFormatFor() == ECF_Json)
                BreakQuotedExec(nextsubst.Name, quoteDelim, false);
            else
                BreakQuotedEval(nextsubst.Name, quoteDelim, false);
            SBDIAG << "SDJS: " << nextsubst.Name << "\n";
        }

        if (substMode == ESM_DoSubst && cmdAcceptor) { // undesirable during ()'s args expansion
            if (Y_UNLIKELY(macro.Flags.Get(EMF_CommandSeparator))) {
                SBDIAG << "SD sep &&\n";
                if (!breakQuote && formatFor == ECF_Json) {
                    BreakQuotedExec(result, "\", \"", false);
                }
                cmdAcceptor->FinishCommand(result);
                cmdAcceptor->StartCommand(result);
                continue;
            }
            if (macro.Flags.Any({EMF_WorkDir, EMF_AsStdout, EMF_SetEnv, EMF_ResourceUri, EMF_TaredOut}) ||
                !macro.RawString && pvar && ((*pvar)[cnt].WorkDir || (*pvar)[cnt].AsStdout || (*pvar)[cnt].SetEnv || (*pvar)[cnt].ResourceUri || (*pvar)[cnt].TaredOut)) {
                // note that DoSubst is a kind of final substitution,
                // DstStart & DstEnd will not be used any more so we can trash them.
                SBDIAG << "SD prop\n";
                auto& cmd = cmdAcceptor->Commands.back();
                if (macro.Flags.Get(EMF_WorkDir) || !macro.RawString && pvar && (*pvar)[cnt].WorkDir) {
                    cmd.Cwd = nextsubst.Name;
                    continue;
                } else if (macro.Flags.Get(EMF_AsStdout) || !macro.RawString && pvar && (*pvar)[cnt].AsStdout) {
                    cmd.StdOut = nextsubst.Name;
                    continue;
                } else if (macro.Flags.Get(EMF_SetEnv) || !macro.RawString && pvar && (*pvar)[cnt].SetEnv) {
                    cmd.EnvSetDefs.push_back(SubstMacroDeeply(nullptr, nextsubst.Name, vars, false));
                    continue;
                } else if (macro.Flags.Get(EMF_ResourceUri) || !macro.RawString && pvar && (*pvar)[cnt].ResourceUri) {
                    cmd.ResourceUris.push_back(SubstMacroDeeply(nullptr, nextsubst.Name, vars, false));
                    continue;
                } else if (macro.Flags.Get(EMF_TaredOut) || !macro.RawString && pvar && (*pvar)[cnt].TaredOut) {
                    cmd.TaredOuts.push_back(SubstMacroDeeply(nullptr, nextsubst.Name, vars, false));
                }
            }
        }

        if (hasPrev) {
            if (macro.Flags.Get(EMF_NoSpaceJoin)) {
                result += macro.NoSpaceJoin;
            } else if (breakQuoteNE && nextsubst.Name.empty()) {
                (void)result; // nothing
            } else if (breakQuote) {
                result += quoteDelim;
            } else {
                result += " ";
            }
        } else if (macro.PrependQuoteDelim) {
            if (nextsubst.Name.empty()) {
                continue; // prevent setting hasPrev
            }
            result += quoteDelim;
        }

        SBDIAG << "SD**: " << nextsubst.Name << "\n";
        if (macro.GetFormatFor() == ECF_Json) {
            // Perform appropriate escaping for a JSON element
            BreakQuotedExec(nextsubst.Name, "", false, '\"');
        }
        if (substObserver) {
            substObserver(nextsubst);
        }
        result += !pvar && macro.Flags.Get(EMF_Quote) ? RemoveMod(EMF_Quote, nextsubst.Name) : nextsubst.Name;
        hasPrev = true;
    }
    if (quote)
        result += "\"";
    if (macro.Flags.Get(EMF_HashVal)) {
        const auto hash = MD5::Calc(TStringBuf{result}.substr(substDataOffset));
        result.resize(substDataOffset);
        result += hash;
    }
    SBDIAG << "SD: " << macro.Name << "(" << num << ") --> " << result << "<---\n";
}

TString TCommandInfo::SubstMacro(const TYVar* origin, TStringBuf pattern, TVector<TMacroData>& macros, ESubstMode substMode, const TVars& subst, ECmdFormat cmdFormat, ECmdFormat formatFor) {
    TDbgPadHelper MsgPad(MsgDepth, ::MsgPad);
    if (MsgDepth > 30) {
        ythrow yexception() << "Macro call stack exceeded the limit" << Endl;
    }

    if (substMode != ESM_DoSubst) {
        FillCoords(origin, macros, substMode, subst, cmdFormat);
        if (substMode == ESM_DoFillCoord) { // not actual subst, ret. value not used
            return TString();
        }
    }

    TString result;
    decltype(MkCmdAcceptor) cmdAcceptor = (EqualToOneOf(cmdFormat, ECF_ExpandSimpleVars, ECF_ExpandVars, ECF_ExpandFoldableVars)) ? nullptr : MkCmdAcceptor;
    bool makeCmd = substMode == ESM_DoSubst && !OnlyMacroCall(macros) && !OnlySelfCall(macros, pattern) && cmdAcceptor && !NoMakeCommand(cmdFormat);
    bool makeJson = makeCmd && cmdFormat == ECF_Json;
    ECmdFormat originalFormat = cmdFormat;
    if (makeCmd) {
        SBDIAG << "SM to MkCmd: " << pattern << "\n";
        cmdFormat = ECF_Unset; // inner calls will have usual substitution
        cmdAcceptor->Start(pattern, result);
        cmdAcceptor->StartCommand(result);
    } else {
        SBDIAG << "SM start: " << pattern << "\n";
    }

    if (macros.empty() && !makeCmd) {
        //SBDIAG << "SM empty\n";
        return TString{pattern};
    }
    SBDIAG << "SM " << pattern << Endl;

    size_t last = 0;
    for (auto& macroData : macros) {
        if (macroData.ComplexArg) {
            continue;
        }

        //YDIAG(Dev) << "SM " << pat.SubStr(last, ~macroData.OrigFragment - ~pat - last) << "\n";
        TStringBuf text = pattern.SubStr(last, macroData.OrigFragment.data() - pattern.data() - last);
        if (makeJson) {
            macroData.FormatFor = ECF_Json;
            cmdAcceptor->ConvertText(result, text);
            cmdAcceptor->ConvertMacro(result, macroData);
        } else {
            result += text;
        }

        last = macroData.OrigFragment.end() - pattern.data();

        //SBDIAG << "SM*" << macroData.Name << "\n";
        macroData.DstStart = result.size();
        if (!SubstData(origin, macroData, subst, macroData.Flags.Get(EMF_KeyValue) ? originalFormat : cmdFormat, substMode, result, formatFor)) {
            result += macroData.OrigFragment;
            //SBDIAG << "->OF " << macroData.OrigFragment << "\n";
        } else {
            bool emptyExp = macroData.DstStart == result.size() || macroData.Flags.Get(EMF_CommandSeparator);
            if (makeCmd && !emptyExp) {
                cmdAcceptor->InToken = cmdAcceptor->ContinueToken = true;
            }
            //YDIAG(Dev) << "->M  " << macroData.Name << "\n";
        }
        macroData.DstEnd = result.size();
    }

    if (makeJson) {
        cmdAcceptor->ConvertText(result, pattern.SubStr(last));
    } else {
        result += pattern.SubStr(last);
    }

    if (makeCmd) {
        cmdAcceptor->FinishCommand(result);
        cmdAcceptor->Finish(result, *this, subst);
    }

    // SBDIAG << "\n+.......................\n" << result << "\n-.......................\n";
    return result;
}

TString TCommandInfo::SubstMacro(const TYVar* origin, TStringBuf pattern, ESubstMode substMode, const TVars& subst, ECmdFormat cmdFormat, bool patHasPrefix, ECmdFormat formatFor) {
    TVector<TMacroData> macros;

    GetMacrosFromPattern(pattern, macros, patHasPrefix);
    return SubstMacro(origin, patHasPrefix ? GetCmdValue(pattern) : pattern, macros, substMode, subst, cmdFormat, formatFor);
}

TString TCommandInfo::SubstMacroDeeply(const TYVar* origin, const TStringBuf& macro, const TVars& vars, bool patternHasPrefix, ECmdFormat cmdFormat) {
    TString res(macro);
    TString modifiedRes = res;
    bool haveToSubst = res.find('$') != TString::npos;
    if (!haveToSubst) {
        return patternHasPrefix ? TString{GetCmdValue(res)} : res;
    }

    //YDIAG(DG) << "Subst into: " << res << Endl;

    static constexpr size_t ITER_LIMIT = 128;// maximum iterations count for macro subst
    size_t iter = 0;
    while (haveToSubst && modifiedRes.size()) {
        TVector<TMacroData> macros;
        GetMacrosFromPattern(res, macros, patternHasPrefix);
        modifiedRes = SubstMacro(origin, patternHasPrefix ? GetCmdValue(res) : res, macros, ESM_DoSubst, vars, cmdFormat);
        patternHasPrefix = false;
        if (modifiedRes != res) {
            res = modifiedRes;
        } else {
            haveToSubst = false;
        }
        if (++iter >= ITER_LIMIT) {
            ythrow yexception() << "Macro subst exceeded the limit " << ITER_LIMIT << Endl;
            break;
        }
    }
    return modifiedRes;
}

TString TCommandInfo::SubstVarDeeply(const TStringBuf& varName, const TVars& vars, ECmdFormat cmdFormat) {
    const TYVar* var = vars.Lookup(varName);
    if (!Get1(var)) {
        return TString();
    }

    return SubstMacroDeeply(var, Get1(var), vars, true, cmdFormat);
}

void TCommandInfo::WriteRequirements(TStringBuf reqs) {
    // see TCommandInfo::ApplyMods / EMF_Requirements
    if (!Requirements) {
        if (Conf) {
            Requirements = MakeHolder<THashMap<TString, TString>>(Conf->GetDefaultRequirements());
        } else {
            Requirements = MakeHolder<THashMap<TString, TString>>();
        }
    }
    ParseRequirements(reqs, *Requirements);
}

void ConvertSpecFiles(const TBuildConfiguration& conf, TSpecFileArr& flist, TYVar& dst) {
    for (auto& f : flist) {
        f.Name = conf.RealPath(ArcPath(f.Name));
        f.IsPathResolved = true;
        dst.push_back(f);
    }
}

void TCommandInfo::GetDirsFromOpts(const TStringBuf opt, const TVars& vars, THolder<TVector<TStringBuf>>& dst) {
    auto dirs = MakeHolder<TVector<TStringBuf>>();
    if (!opt.empty()) {
        TStringBuf optsSubst = Conf->GetStringPool()->Append(SubstMacroDeeply(nullptr, opt, vars, false));
        Split(optsSubst, " ", *dirs);
    }
    if (dst)
        dst->insert(dst->end(), dirs->begin(), dirs->end());
    else
        dst = std::move(dirs);
}

void TCommandInfo::ApplyToolOptions(const TStringBuf macroName, const TVars& vars) {
    auto j = Conf->BlockData.find(macroName);
    const TToolOptions* toolOptions = j ? j->second.ToolOptions.Get() : nullptr;
    if (toolOptions) {
        if (!toolOptions->AddPeers.empty()) {
            SBDIAG << "ADDPEERS mined from '" << macroName << ": " << toolOptions->AddPeers << Endl;
            GetDirsFromOpts(toolOptions->AddPeers, vars, AddPeers);
        }
        if (!toolOptions->AddIncl.empty()) {
            SBDIAG << "ADDINCLS mined from '" << macroName << ": " << toolOptions->AddIncl << Endl;
            GetDirsFromOpts(toolOptions->AddIncl, vars, AddIncls);
        }
    }
}
