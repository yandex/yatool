#include "module_loader.h"
#include "macro_string.h"

#include "args2locals.h"
#include "builtin_macro_consts.h"
#include "makefile_loader.h"
#include "prop_names.h"
#include "ymake.h"

#include <devtools/ymake/lang/plugin_facade.h>

#include <devtools/ymake/diag/manager.h>
#include <devtools/ymake/diag/trace.h>

#include <library/cpp/iterator/mapped.h>


TModuleDef::TModuleDef(TYMake& yMake, TModule& module, const TModuleConf& conf)
    : YMake(yMake)
    , Conf(yMake.Conf)
    , Names(yMake.Names)
    , Modules(yMake.Modules)
    , Module(module)
    , ModuleConf(conf)
    , Vars(module.Vars)
    , Makefile(module.GetMakefile().GetTargetStr())
{
}

void TModuleDef::InitModuleSpecConditions() {
    for (auto i : ModuleConf.SpecServiceVars)
        SetSpecServiceVars(TString::Join("env", i));
}

size_t TModuleDef::StatementPriority(const TStringBuf& s) {
    if (s == "SRCS"sv || s == "_GO_PROCESS_SRCS"sv || s == "PY_SRCS"sv) {
        return 4;
    }

    if (s == TStringBuf("_ADD_PY_LINTER_CHECK")) {
        return 5;
    }

    if (s == NMacro::PEERDIR || s == NMacro::_GHOST_PEERDIR || s == NMacro::SRCDIR || s == NMacro::_LATE_GLOB) { //todo: move in config
        return 1;
    }

    if (s == TStringBuf("ADD_COMPILABLE_BYK") ||
        s == TStringBuf("JAVA_MODULE") || s == TStringBuf("JAVA_TEST") ||
        s == TStringBuf("SETUP_EXECTEST") ||
        s == TStringBuf("PROCESS_DOCS") ||
        s == TStringBuf("PROCESS_DOCSLIB")) {
        return 3;
    }

    return 2;
}

void TModuleDef::SetSpecServiceVars(const TString& specVar) { //allow translate macro call to variable changing
    if (!specVar.empty()) {
        YDIAG(DG) << "Need to recalc conditions with " << specVar << Endl;
        Vars.SetValue(specVar, "yes");
        Conf.Conditions.RecalcVars(TString::Join("$", specVar), Vars, OrigVars);
    }
}

bool TModuleDef::IsInput(const TStringBuf& sname, const TStringBuf& name) const {
    // TODO: per-module-type property, for example:
    // inputs = SRCS[.o];
    if (sname != TStringBuf("SRCS"))
        return false;
    return ModuleConf.IsInput(NPath::Extension(name));
}

bool TModuleDef::IsGlobalInput(const TStringBuf& name) const {
    return ModuleConf.IsGlobalInput(NPath::Extension(name));
}

const TModuleDef::TMacroCalls* TModuleDef::PrepareMacroBody(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& locals) {
    // Find macro to call
    auto pi = Conf.BlockData.find(name);
    if (!pi || !pi->second.CmdProps || !pi->second.CmdProps->HasMacroCalls()) {
        return nullptr;
    }
    const auto& props = *pi->second.CmdProps;
    AddMacroArgsToLocals(props, args, locals, *MakeFileMap.Pool).or_else([&](const TMapMacroVarsErr& err) -> TMapMacroVarsResult {
        err.Report(name, JoinStrings(args.begin(), args.end(), ", "));
        return {};
    }).value();
    return &props.GetMacroCalls();
}

TStringBuf TModuleDef::PrepareMacroCall(const TMacroCall& macroCall, const TVars& locals, TSplitString& callArgs, const TStringBuf& name) {
    callArgs = TCommandInfo(Conf, nullptr, nullptr).SubstMacroDeeply(nullptr, macroCall.second, locals);
    callArgs.Split(' ');
    // Take appropriate macro specialization name
    TStringBuf macroName = Conf.GetSpecMacroName(macroCall.first, callArgs);
    YDIAG(DG) << "Processing macro call from " << name << " to " << macroName << Endl;
    YDIAG(DG) << "Args: " << TVecDumpSb(callArgs);
    return macroName;
}

bool TModuleDef::ProcessBaseMacro(const TStringBuf& macroName, const TVector<TStringBuf>& args, const TStringBuf& name) {
    if (TCmdProperty::IsBaseMacroCall(macroName) && (TDirParser::SetStatement(macroName, args, Vars, OrigVars) ||
            ProcessGlobStatement(macroName, args, Vars, OrigVars))) {
        YDIAG(DG) << "Recalc conditions after base macro call: " << name << Endl;
        Conf.Conditions.RecalcVars(TString::Join("$", args[0]), Vars, OrigVars);
        return true;
    }
    if (TDirParser::MessageStatement(macroName, args, Vars, Conf)) {
        return true;
    }
    if (macroName == NMacro::_NEVERCACHE) {
        NeverCache = true;
        return true;
    }
    return false;
}

// Called on user-specified statements only(statements from makelist)
void TModuleDef::ProcessMakelistStatement(const TStringBuf& name, const TVector<TStringBuf>& args) {
    if (name == NMacro::PEERDIR) {
        for (const TStringBuf& arg: args) {
            if (arg == NMacro::ADDINCL || arg == TStringBuf("GLOBAL")) {
                continue;
            }

            TString dir = NPath::IsExternalPath(arg) ? TString{arg} : NPath::ConstructYDir(arg, TStringBuf(), ConstrYDirDiag);
            if (!dir.empty()) {
                MakelistPeers.Push(dir);
                if (!NPath::IsExternalPath(dir)) {
                    TFileView dirEnt = Names.FileConf.GetStoredName(dir);
                    FORCE_UNIQ_CONFIGURE_TRACE(dirEnt, H, NEvent::TNeedDirHint(TString{NPath::CutType(dir)}));
                }
            }
        }
    }
}

bool TModuleDef::AddStatement(const TStringBuf& name, TArrayRef<const TStringBuf> args) {
    if (IsMacroIgnored(name)) {
    } else if (IsMacroAllowed(name)) {
        MakeFileMap.Add(StatementPriority(name), name, args, IsMulti(name));
        return true;
    } else {
        YConfErr(Misconfiguration) << name << " for " << GetName() << " is not allowed for " << Module.GetUserType() << " modules!" << Endl;
    }
    return false;
}

void TModuleDef::InitModule(const TStringBuf& name, TArrayRef<const TStringBuf> args, TVars& vars, TOriginalVars& orig) {
    // set outer namespace to persistent storage
    Vars = vars;
    Vars.SetValue(NVariableDefs::VAR_MANGLED_MODULE_TYPE, name);
    if (Module.IsFromMultimodule()) {
        Vars.SetValue(NVariableDefs::VAR_MODULE_KIND, name.SubStr(name.rfind(MODULE_MANGLING_DELIM) + MODULE_MANGLING_DELIM.size()));
    } else {
        Vars.SetValue(NVariableDefs::VAR_MODULE_KIND, name);
    }
    Vars.SetValue(NVariableDefs::VAR_MODDIR, Module.GetDir().CutType());
    const auto& moduleConf = ModuleConf;
    Vars.AssignFilterGlobalVarsFunc([&moduleConf](const TStringBuf &varName) -> bool { return moduleConf.Globals.contains(varName); });
    OrigVars = orig;
    InitModuleSpecConditions();
    ProcessModuleCall(name, args);
    InitFromConf();
    //TODO: set this variable only for DLL and derivatives or add common variable to store FileName
    Vars.SetValue(NVariableDefs::VAR_SONAME, Module.GetFileName());
    Vars.SetValue(NVariableDefs::VAR_MODULE_ARGS, JoinStrings(args.cbegin(), args.cend(), " "));
    YDIAG(DG) << "Module dep: " << Module.GetName() << Endl;
}

bool TModuleDef::IsUserMacro(const TStringBuf& name) const {
    auto i = Conf.BlockData.find(name);
    return i && i->second.IsUserMacro;
}

//TODO: rewrite this with inverted logic!
// what really should be here? SRCS and all DirStatements(PEERDIR, SRCDIR etc). what else?
bool TModuleDef::IsMulti(const TStringBuf& name) const {
    return name == TStringBuf("ALLOCATOR")
           || name == TStringBuf("INDUCED_DEPS")
           || name == TStringBuf("GENERATE_PY_PROTOS")
           || name == TStringBuf("RUN_JAVA_PROGRAM")
           || name == TStringBuf("SPLIT_CODEGEN")
           || name == TStringBuf("CHECK_DEPENDENT_DIRS")
           || name == TStringBuf("DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE")
           || name == TStringBuf("DECLARE_EXTERNAL_HOST_RESOURCES_PACK")
           || name == NMacro::_LATE_GLOB
           || IsUserMacro(name) || Conf.ContainsPluginMacro(name);
}

TDepsCacheId TModuleDef::Commit() {
    TFileView modName = Module.GetName();
    ui32 modId = modName.GetElemId();

    ConfMsgManager()->Erase(modId);

    if (LateConfErrNoSem_) {
        TScopedContext scopedContext{modName};
        YConfErr(NoSem) << "No semantics set for " << Module.GetUserType() << ". Module is not intended to be exported." << Endl;
    }

    const TModule* other = Modules.Get(modId);
    if (Y_UNLIKELY(other != nullptr && other != &Module && !other->IsLoaded())) {
        YConfErr(UserErr) << "module name [[alt1]]" << modName << "[[rst]] already used, use .SUFFIX/.PREFIX properties to disambiguate" << Endl;
        YConfWarn(UserErr) << "First use in directory [[alt1]]" << other->GetDir() << "[[rst]], variant: [[alt1]]" << other->GetTag() << "[[rst]]" << Endl;
        YConfWarn(UserErr) << "Second use in directory [[alt1]]" << Module.GetDir() << "[[rst]], variant: [[alt1]]" << Module.GetTag() << "[[rst]]" << Endl;
        return TDepsCacheId::None;
    }

    if (Vars.Contains(NVariableDefs::VAR_PEERDIR_TAGS)) {
        TString expandedTags = TCommandInfo(Conf, nullptr, nullptr).SubstVarDeeply(NVariableDefs::VAR_PEERDIR_TAGS, Vars);
        Module.Set(NVariableDefs::VAR_PEERDIR_TAGS, expandedTags);
    }

    Module.FinalizeConfig(modId, ModuleConf);
    Modules.Commit(Module);
    YMake.UpdIter->SaveModule(modId, this);
    return MakeDepFileCacheId(modId);
}

void TModuleDef::InitFromConf() {
    TStringBuf baseName = GetCmdValue(Vars.Get1(NVariableDefs::VAR_REALPRJNAME));
    Y_ASSERT(!baseName.empty());
    if (baseName.Contains('/')) {
        YConfErr(Misconfiguration) << "Invalid module name '" << Module.Get("REALPRJNAME") << "'. Symbol '/' is not allowed" << Endl;
    }
    TString prefix = Vars.Contains(NVariableDefs::VAR_MODULE_PREFIX) ? TCommandInfo(Conf, nullptr, nullptr).SubstVarDeeply(NVariableDefs::VAR_MODULE_PREFIX, Vars) : "";
    TString ext = Vars.Contains(NVariableDefs::VAR_MODULE_SUFFIX) ? TCommandInfo(Conf, nullptr, nullptr).SubstVarDeeply(NVariableDefs::VAR_MODULE_SUFFIX, Vars) : "";
    TString globalExt = Vars.Contains(NVariableDefs::VAR_GLOBAL_SUFFIX) ? TCommandInfo(Conf, nullptr, nullptr).SubstVarDeeply(NVariableDefs::VAR_GLOBAL_SUFFIX, Vars) : "";

    TString fileName = TString::Join(prefix, baseName, ext);
    TString globalFileName = TString::Join(prefix, baseName, globalExt);
    Module.Init(fileName, globalFileName, TString{baseName}, ModuleConf);
}

bool TModuleDef::ProcessGlobStatement(const TStringBuf& name, const TVector<TStringBuf>& args, TVars& vars,
                                      TOriginalVars& orig, std::pair<size_t, size_t> location) {
    if (name != NMacro::_GLOB) {
        return false;
    }

    TStringBuf globPropName = NProps::GLOB;

    if (args.empty()) {
        YConfErrPrecise(Syntax, location.first, location.second) << "empty argument in [[alt1]]" << name << "[[rst]]" << Endl;
        return true;
    }

    TStringBuf varName = args.front();
    const auto [globsWithExcludes, restrictions] = SplitBy(TArrayRef<const TStringBuf>{args}.subspan(1), NArgs::RESTRICTIONS);
    TGlobRestrictions globRestrictions;
    if (!restrictions.empty()) {
        globRestrictions = ParseGlobRestrictions(restrictions.subspan(1), NMacro::_GLOB);
    }
    if (IsExtendGlobRestriction()) {
        globRestrictions.Extend();
    }
    const auto [globs, excludes] = SplitBy(TArrayRef<const TStringBuf>{globsWithExcludes}, NArgs::EXCLUDE);
    Y_UNUSED(globRestrictions);

    TUniqVector<ui32> excludeIds;
    TExcludeMatcher excludeMatcher;
    for (auto pattern: excludes) {
        if (pattern == NArgs::EXCLUDE) {
            continue;
        }
        if (excludeIds.Push(Names.AddName(EMNT_Property, FormatProperty(NProps::GLOB_EXCLUDE, pattern)))) {
            excludeMatcher.AddExcludePattern(Module.GetDir(), pattern);
        }
    }

    TGlobStat globStat;
    TUniqVector<TFileView> values;
    for (auto globStr : globs) {
        try {
            TUniqVector<ui32> matches;
            TGlob glob(Names.FileConf, globStr, Module.GetDir());
            TGlobStat patternStat;
            for (const auto& result : glob.Apply(excludeMatcher, &patternStat)) {
                values.Push(result);
            }
            globStat += patternStat;

            const auto globCmd = FormatCmd(Module.GetName().GetElemId(), globPropName, globStr);
            const auto globId = Names.AddName(EMNT_BuildCommand, globCmd);
            const auto globHash = Names.AddName(EMNT_Property, FormatProperty(NProps::GLOB_HASH, glob.GetMatchesHash()));
            const auto refferer = Names.AddName(EMNT_Property, FormatProperty(NProps::REFERENCED_BY, varName));
            ModuleGlobs.push_back(TModuleGlobInfo{globId, globHash, glob.GetWatchDirs().Data(), matches.Take(), excludeIds.Data(), refferer});
        } catch (const yexception& error){
            YConfErrPrecise(Syntax, location.first, location.second) << "Invalid pattern in [[alt1]]" << name << "[[rst]]: " << error.what() << Endl;
        }
    }

    if (Conf.CheckGlobRestrictions) {
        globRestrictions.Check(name, globStat);
    }

    auto&& range = MakeMappedRange(values, [](auto x) {
        Y_ASSERT(x.IsType(NPath::Source));
        return TString::Join("${ARCADIA_ROOT}/", x.CutType());
    });
    const auto varValue = JoinStrings(range.begin(), range.end(), " ");
    YDIAG(VV) << "GLOB result: " << varName << " = " << varValue << Endl;
    vars.SetStoreOriginals(varName, varValue, orig);
    return true;
}

TGlobRestrictions TModuleDef::ParseGlobRestrictions(const TArrayRef<const TStringBuf>& restrictions, const TStringBuf& macro) {
    const auto rend = restrictions.cend();
    auto parseVal = [&macro, &rend](TArrayRef<const TStringBuf>::iterator& rit, const TStringBuf& name) {
        if (*rit != name) {
            return -1;
        }
        if ((rit + 1) == rend) {
            YConfErr(Syntax) << "No value for " << name << " in " << macro << Endl;
            return -2;
        }
        rit++;
        int v = 0;
        try {
            v = std::stoi(std::string(*rit).c_str());
        } catch (const std::exception) {
        }
        if (v <= 0 || v > 1000000) {
            YConfErr(Syntax) << "Invalid value " << *rit << " for " << name << " in " << macro << Endl;
            return -3;
        }
        return v;
    };

    TGlobRestrictions globRestrictions;
    auto rit = restrictions.cbegin();
    while (rit != rend) {
        auto maxMatches = parseVal(rit, NArgs::MAX_MATCHES);
        if (maxMatches > 0) {
            globRestrictions.MaxMatches = maxMatches;
        }
        auto maxWatchDirs = parseVal(rit, NArgs::MAX_WATCH_DIRS);
        if (maxWatchDirs > 0) {
            globRestrictions.MaxWatchDirs = maxWatchDirs;
        }
        if (maxMatches == -1 && maxWatchDirs == -1) {
            YConfErr(Syntax) << "Unknown restriction name " << *rit << " in " << macro << Endl;
        }
        rit++;
    }
    return globRestrictions;
}

bool TModuleDef::IsExtendGlobRestriction() const {
    return !Conf.GlobRestrictionExtends.empty() && Conf.GlobRestrictionExtends.contains(Module.GetDir().CutType());
}

const TVector<TModuleGlobInfo>& TModuleDef::GetModuleGlobs() const {
    return ModuleGlobs;
}
