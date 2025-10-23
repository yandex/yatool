#include "conf.h"

#include "sysincl_conf.h"
#include "licenses_conf.h"
#include "autoincludes_conf.h"

#include <devtools/ymake/common/memory_pool.h>
#include <devtools/ymake/lang/confreader_cache.h>
#include <devtools/ymake/macro.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/macro_string.h>
#include <devtools/ymake/plugins/cpp_plugins.h>
#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/vardefs.h>
#include <devtools/ymake/yndex/builtin.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/stats.h>
#include <devtools/ymake/diag/stats_enums.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/common/memory_pool.h>

#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/string/cast.h>
#include <util/string/split.h>

using namespace NYMake;

namespace {
    constexpr TStringBuf VAR_PEERDIRS_RULES_PATH = "PEERDIRS_RULES_PATH"sv;
    constexpr TStringBuf VAR_FAKEID = "FAKEID"sv;
    constexpr TStringBuf VAR_BLACKLISTS = "_BLACKLISTS"sv;
    constexpr TStringBuf VAR_ISOLATED_PROJECTS = "_ISOLATED_PROJECTS"sv;
    constexpr TStringBuf VAR_DEFAULT_REQUIREMENTS = "DEFAULT_REQUIREMENTS"sv;
    constexpr TStringBuf VAR_EXCLUDED_PEERDIRS = "EXCLUDED_PEERDIRS"sv;
    constexpr TStringBuf VAR_FOLDABLE_VARS = "_FOLDABLE_VARS"sv;
    constexpr TStringBuf VAR_EXPORT_SRC_ROOT = "EXPORT_SOURCE_ROOT"sv;
    constexpr TStringBuf VAR_AUTOINCLUDE_PATHS = "AUTOINCLUDE_PATHS"sv;

    void FoldGlobalCommands(TBuildConfiguration& conf) {
        TTraceStage stage("Fold global commands");
        auto& vars = conf.CommandConf;

        auto foldableVarsList = TCommandInfo(conf, nullptr, nullptr).SubstVarDeeply(VAR_FOLDABLE_VARS, vars);

        TVector<TYVar*> tempDontExpandVars;
        for (auto& [_, var] : vars) {
            if (!var.DontExpand) {
                var.DontExpand = true;
                tempDontExpandVars.push_back(&var);
            }
        }

        for (const auto name : StringSplitter(foldableVarsList).Split(' ').SkipEmpty()) {
            if (auto iter = vars.find(name); iter != vars.end()) {
                iter->second.DontExpand = false;
            }
        }

        for (auto& [name, var] : vars) {
            if (var.size() == 1 && var[0].Name.StartsWith('0')) {
                ui64 id;
                TStringBuf varName;
                TStringBuf varValue;
                ParseCommandLikeVariable(var[0].Name, id, varName, varValue);
                if (id != 0 || GetMacroType(varValue) == EMT_MacroDef) {
                    continue;
                }
                auto newValue = TCommandInfo(conf, nullptr, nullptr).SubstMacroDeeply(nullptr, varValue, vars, false, ECF_ExpandFoldableVars);
                if (newValue != varValue) {
                    var.SetSingleVal(TString(varName), newValue, id, vars.Id);
                }
            }
        }

        for (auto var : tempDontExpandVars) {
            var->DontExpand = false;
        }
    }

    void FillModuleScopeOnlyFlag(TBuildConfiguration& conf) {
        TTraceStage stage("Fill ModuleScopeOnly flag");
        auto& vars = conf.CommandConf;

        auto moduleScopeOnlyVarList = TCommandInfo(conf, nullptr, nullptr).SubstVarDeeply(NVariableDefs::VAR__MODULE_SCOPE_ONLY_VARS, vars);

        for (const auto name : StringSplitter(moduleScopeOnlyVarList).Split(' ').SkipEmpty()) {
            if (auto iter = vars.find(name); iter != vars.end()) {
                iter->second.ModuleScopeOnly = true;
            }
        }
    }

    void ReadModulesWithExtendedGlobs(TBuildConfiguration& conf) {
        TVector<TStringBuf> extends;
        Split(conf.CommandConf.EvalValue(NVariableDefs::VAR_MODULES_WITH_EXTENDED_GLOB_RESTRICTIONS), " ", extends);
        for (const auto& extend: extends) {
            if (extend.empty()) {
                continue;
            }
            conf.GlobRestrictionExtends.insert(extend);
        }
    }

    void ReadFeatureFlags(TBuildConfiguration& conf) {
        conf.FillModule2Nodes = NYMake::IsTrue(conf.CommandConf.EvalValue("FILL_MODULE2NODES"));
        conf.CheckGlobRestrictions = NYMake::IsTrue(conf.CommandConf.EvalValue("CHECK_GLOB_RESTRICTIONS"));
        decltype(conf.GlobSkippedErrorPercent) globSkippedErrorPercent;
        if (TryFromString(conf.CommandConf.EvalValue("GLOB_SKIPPED_ERROR_PERCENT"), globSkippedErrorPercent)) {
            conf.GlobSkippedErrorPercent = globSkippedErrorPercent;
        }
    }
}

TBuildConfiguration::TBuildConfiguration() {
    StrPool = IMemoryPool::Construct();
}

void TBuildConfiguration::AddOptions(NLastGetopt::TOpts& opts) {
    TCommandLineOptions::AddOptions(opts);
    TStartUpOptions::AddOptions(opts);
    TDebugOptions::AddOptions(opts);

    opts.AddLongOption("sem-graph", "dump semantic graph instead of build plan").SetFlag(&RenderSemantics).NoArgument();
    opts.AddLongOption("foreign-on-nosem", "on NoSem error make foreign request instead configure error").SetFlag(&ForeignOnNoSem).NoArgument();
    opts.AddLongOption('w', "warn-level", "level of human-readable messages to be shown (0 or more: none, error, warning, info, debug)").StoreResult(&WarnLevel);
    opts.AddLongOption('W', "warn", "warnings & messages to display").AppendTo(&WarnFlags);
    opts.AddLongOption('y', "plugins-root", "set plugins root").SplitHandler(&PluginsRoots, ',');
    opts.AddLongOption('Q', "dump-custom-data", "<type0>:<output file0>;<type1>:<output file1>...<typen>:<output filen> - generate custom data").StoreResult(&CustomData);
}

void TBuildConfiguration::CompileAndRecalcAllConditions() {
    TTraceStage stage("Compile and RecalcAll conditions");
    TAutoPtr<IMemoryPool> pool = IMemoryPool::Construct();
    TVector<char*> conditions;
    for (const auto& s: Conditions.GetRawConditions()) {
        conditions.push_back((char*)pool->Append(s).data());
    }
    Conditions.ConditionCalc.Compile(conditions.data(), conditions.size());
    Conditions.RecalcAll(CommandConf);
}

void TBuildConfiguration::PrepareConfiguration(TMd5Sig& confMd5) {
    using namespace NConfReader;

    auto updateConfCacheFlags = [this]() {
        if (!NYMake::IsTrue(CommandConf.EvalValue("CONF_CACHE_ENABLED"sv))) {
            DisableConfCache();
        }
        else if (NYMake::IsTrue(CommandConf.EvalValue("DEPS_CACHE_CONTROL_CONF_CACHE"sv))) {
            MakeDepsCacheControlConfCache();
        }
    };

    ClearYmakeConfig();
    Y_ASSERT(!GetFromCache());

    bool fromCache = false;
    if (ReadConfCache && LoadCache(*this, confMd5) == ELoadStatus::Success) {
        updateConfCacheFlags();
        // call to updateConfCacheFlag() may change the value of ReadConfCache
        if (ReadConfCache) {
            fromCache = true;
        } else {
            ClearYmakeConfig();
        }
    }
    Y_ASSERT(GetFromCache() == fromCache);

    if (fromCache) {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedConfCache), true);
    } else {
        NStats::TStatsBase::MonEvent(MON_NAME(EYmakeStats::UsedConfCache), false);
        MD5 tempConfData;
        NYMake::TTraceStage stage("Load configuration (no cache)");
        LoadConfig(YmakeConf.GetPath(), SourceRoot.GetPath(), BuildRoot.GetPath(), tempConfData);
        tempConfData.Final(confMd5.RawData);
        updateConfCacheFlags();
    }

    if (WriteConfCache && !fromCache) {
        SaveCache(*this, confMd5);
    } else if (!WriteConfCache) {
        // FIXME(snermolaev): Shuld we delete previous cache when cache is not saved
        // confManager.RemoveCache(*this);
    }
}

void TBuildConfiguration::PostProcessCacheOptions() {
    UseOnlyYmakeCache_ = CachePath.IsDefined();
    if (CachePath.IsDefined()) {
        WriteFsCache = false;
        WriteDepsCache = false;
        WriteJsonCache = false;
        WriteDepManagementCache = false;
        WriteUidsCache = false;
    } else {
        CachePath = YmakeCache;
    }
    auto dumpCacheFlags = [](const char* cacheName, bool rFlag, bool wFlag) {
        if (rFlag) {
            YDebug() << cacheName << " cache loading is enabled" << Endl;
        }
        if (wFlag) {
            YDebug() << cacheName << " cache saving is enabled" << Endl;
        }
    };
    dumpCacheFlags("Conf", ReadConfCache, WriteConfCache);
    dumpCacheFlags("FS", ReadFsCache, WriteFsCache);
    dumpCacheFlags("Deps", ReadDepsCache, WriteDepsCache);
    dumpCacheFlags("DepManagement", ReadDepManagementCache, WriteDepManagementCache);
    dumpCacheFlags("Json", ReadJsonCache, WriteJsonCache);
    dumpCacheFlags("Uids", ReadUidsCache, WriteUidsCache);

    LoadGraph_ = (!RebuildGraph && WriteYdx.empty() || ReadFsCache || ReadDepsCache) && CachePath.Exists();
}

void TBuildConfiguration::PostProcess(const TVector<TString>& freeArgs) {
    if (!DisableHumanReadableOutput) {
        Display()->SetStream(LockedStream());
    }
    if (WarnLevel) {
        auto warnLevel = static_cast<EConfMsgType>(*WarnLevel);
        if (warnLevel >= EConfMsgType::Error && warnLevel <= EConfMsgType::Count)
            Display()->SetCutoff(warnLevel);
    }

    TCyclesTimer runStageTimer;

    Diag()->Init(WarnFlags);

    if (!BinaryLogFileName.empty()) {
        Diag()->BinaryLog = true;
        DebugLogWriter()->SetFile(BinaryLogFileName);
    }

    if (DisableTextLog) {
        Diag()->TextLog = false;
    }

    if (!PatchPath2.empty()) {
        PatchPath = PatchPath2;
        ReadFileContentFromZipatch = ReadFileContentFromZipatch2;
    }

    TCommandLineOptions::PostProcess(freeArgs);
    TStartUpOptions::PostProcess(freeArgs);
    // We must not trace anything before trace stream initialization
    // which is done in StartUpOptions::PostProcess
    RunStageWithTimer = MakeHolder<TTraceStageWithTimer>("ymake run", MON_NAME(EYmakeStats::RunTime), runStageTimer);
    TDebugOptions::PostProcess(freeArgs);
    PostProcessCacheOptions();

    if (CheckDataPaths && !ArcadiaTestsDataRoot.IsDefined()) {
        throw TConfigurationError() << "-b (arcadia tests data path) is not specified";
    }

    if (CompletelyTrustFSCache && !PatchPath) {
        throw TConfigurationError() << "we can use --completely-trust-fs-cache only with changelist/patch --patch-path";
    }

    PrepareBuildDir();

    if (!CustomData.empty()) {
        GenerateCustomData(CustomData);
    }

    MD5 confData, confWoRulesData, rulesData, blacklistHash, isolatedProjectsHash, extraData;

    if (WriteYdx.empty()) {
        CommandDefinitions.Disable();
        CommandReferences.Disable();
    }

    TMd5Sig confMd5;
    PrepareConfiguration(confMd5);
    confData.Update(&confMd5, sizeof(confMd5));
    CompileAndRecalcAllConditions();
    FoldGlobalCommands(*this);
    FillModuleScopeOnlyFlag(*this);
    ReadModulesWithExtendedGlobs(*this);
    ReadFeatureFlags(*this);

    NYndex::AddBuiltinDefinitions(CommandDefinitions);

    // this code hangs on win while trying to lock locked non-recursive mutex
    // (after an error that file is not found)
    if (!DontUsePlugins) {
        if (!PluginsRoots.empty()) {
            TTraceStage stage("Load plugins");
            LoadPlugins(PluginsRoots, UseSubinterpreters, this);
            for (const auto& it : Plugins) {
                TBlob pluginContent = TBlob::FromFile(it);
                confData.Update((const char*)pluginContent.Data(), pluginContent.Size());
            }
        }

        // All cpp plugins should be registered in RegisterCppPlugins()
        NYMake::NPlugins::RegisterCppPlugins(*this);
    }

    CommandConf.SetValue("ARCADIA_BUILD_ROOT", BuildRoot.c_str());

    CompleteModules();
    VerifyModuleConfs();
    confWoRulesData = confData;

    // next conf files don't influence on dep cache
    LoadSystemHeaders(extraData);
    LoadPeersRules(rulesData);
    if (Diag()->BlckLst) {
        // Add blacklist hash to only blacklist hash (new behavior) and rules (current behavior - deprecated) by config switch
        LoadBlackLists(blacklistHash);
        blacklistHash.Final(YmakeBlacklistHash.RawData);
    }
    if (Diag()->IslPrjs) {
        // Add isolated projects hash to only isolated projects hash (new behavior) and rules (current behavior - deprecated) by config switch
        LoadIsolatedProjects(isolatedProjectsHash);
        isolatedProjectsHash.Final(YmakeIsolatedProjectsHash.RawData);
    }
    LoadLicenses(extraData);
    LoadAutoincludes(rulesData);
    TMd5Sig rules;
    rulesData.Final(rules.RawData);
    extraData.Update(rules.RawData);
    confData.Update(rules.RawData);

    FillMiscValues();
    InitExcludedPeerdirs();

    confData.Final(YmakeConfMD5.RawData);
    confWoRulesData.Final(YmakeConfWoRulesMD5.RawData);
    extraData.Final(YmakeExtraConfMD5.RawData);
}

void TBuildConfiguration::PrepareBuildDir() const {
    AssertEx(BuildRoot.IsDirectory() || !BuildRoot.Exists(), "'B' name is not a directory");
    BuildRoot.MkDirs();
}

void TBuildConfiguration::GenerateCustomData(const TStringBuf genCustomData) {
    TVector<TStringBuf> dataTypes;
    Split(TStringBuf(genCustomData), ";", dataTypes);
    for (auto it = dataTypes.begin(); it != dataTypes.end(); ++it) {
        size_t pos = it->find(':');
        if (pos == TString::npos || pos == 0 || pos == (*it).size() - 1)
            ythrow yexception() << "Usage: <type of data>:<output file> but " << *it;
        TString customDataType = TString{it->SubStr(0, pos)};
        customDataType.to_upper();
        TFsPath customDataPath = BuildRoot / it->SubStr(pos + 1);
        customDataPath.DeleteIfExists();
        CustomDataGen[customDataType] = customDataPath;
        YDebug() << "Will write info for " << customDataType << " to " << customDataPath << Endl;
        CommandConf.SetValue(TString::Join(customDataType, "_OUT_FILE"), customDataPath.c_str());
    }
}

void TBuildConfiguration::LoadSystemHeaders(MD5& confData) {
    TString sysinclVar = TCommandInfo(*this, nullptr, nullptr).SubstVarDeeply(TStringBuf("SYSINCL"), CommandConf);
    TVector<TFsPath> sysinclFiles;
    for (const auto& it : StringSplitter(sysinclVar).Split(' ').SkipEmpty()) {
        sysinclFiles.emplace_back(SourceRoot / it.Token());
    }
    Sysincl = ::LoadSystemIncludes(sysinclFiles, confData);
}

void TBuildConfiguration::LoadLicenses(MD5& confData) {
    TString licenses = TCommandInfo(*this, nullptr, nullptr).SubstVarDeeply(TStringBuf("LICENSES"), CommandConf);
    TVector<TFsPath> licensesFiles;
    for (const auto& it : StringSplitter(licenses).Split(' ').SkipEmpty()) {
        licensesFiles.emplace_back(SourceRoot / it.Token());
    }
    Licenses = ::LoadLicenses(licensesFiles, confData);
}

void TBuildConfiguration::LoadAutoincludes(MD5& confData) {
    TString autoincludes = TCommandInfo(*this, nullptr, nullptr).SubstVarDeeply(TStringBuf(VAR_AUTOINCLUDE_PATHS), CommandConf);
    TVector<TFsPath> autoincludeFiles;
    for (const auto& it : StringSplitter(autoincludes).Split(' ').SkipEmpty()) {
        autoincludeFiles.emplace_back(SourceRoot / it.Token());
        AutoincludeJsonPaths.emplace_back(it.Token());
    }
    AutoincludePathsTrie = ::LoadAutoincludes(autoincludeFiles, confData);
}

void TBuildConfiguration::LoadPeersRules(MD5& confData) {
    const auto cmdPeerdirsRulesPath = CommandConf.Get1(VAR_PEERDIRS_RULES_PATH);
    if (!cmdPeerdirsRulesPath) {
        return;
    }

    TVector<TStringBuf> relativePaths;
    Split(GetCmdValue(cmdPeerdirsRulesPath), " ", relativePaths);

    for (const auto& relativePath : relativePaths) {
        const auto path = SourceRoot / relativePath;
        TFileInput fileInput(path);
        const auto content = fileInput.ReadAll();

        confData.Update(content.data(), content.size());

        TPeersRulesReader reader(new TStringInput(content), path);
        PeersRules += reader.Read();
    }

    PeersRules.Finalize();
}

void TBuildConfiguration::LoadBlackLists(MD5& confHash) {
    const TString blacklistsVar = TCommandInfo(*this, nullptr, nullptr).SubstVarDeeply(VAR_BLACKLISTS, CommandConf);
    TVector<TStringBuf> blacklistFiles = StringSplitter(blacklistsVar).Split(' ').SkipEmpty();
    BlackList.Load(SourceRoot, blacklistFiles, confHash);
}

void TBuildConfiguration::LoadIsolatedProjects(MD5& confData) {
    const TString isolatedProjectsVar = TCommandInfo(*this, nullptr, nullptr).SubstVarDeeply(VAR_ISOLATED_PROJECTS, CommandConf);
    TVector<TStringBuf> isolatedProjectsFiles = StringSplitter(isolatedProjectsVar).Split(' ').SkipEmpty();
    IsolatedProjects.Load(SourceRoot, isolatedProjectsFiles, confData);
}

void TBuildConfiguration::FillMiscValues() {
    const char* includeExtsVar = "INCLUDE_EXTS";
    const char* defaultIncludeExts = ".h .hh .hpp .rli .cuh .inc .i";
    const auto includeExtsCmd = CommandConf.Get1(includeExtsVar);
    TStringBuf includeExtsStr = includeExtsCmd ? GetCmdValue(includeExtsCmd) : defaultIncludeExts;
    Split(includeExtsStr, " ", IncludeExts);

    const char* langsVar = "LANGS_REQUIRE_BUILD_AND_SRC_ROOTS";
    const auto langsCmd = CommandConf.Get1(langsVar);
    TStringBuf langsStr = langsCmd ? GetCmdValue(langsCmd) : "";
    Split(langsStr, " ", LangsRequireBuildAndSrcRoots);

    TraverseRecurses = !IsFalse(CommandConf.EvalValue("TRAVERSE_RECURSE"));
    TraverseAllRecurses = TraverseRecurses && NYMake::IsTrue(CommandConf.EvalValue("TRAVERSE_RECURSE_FOR_TESTS"));
    TraverseDepsTests = TraverseAllRecurses && NYMake::IsTrue(CommandConf.EvalValue("RECURSIVE_ADD_PEERS_TESTS"));
    FailOnRecurse = NYMake::IsTrue(CommandConf.EvalValue("FAIL_ON_RECURSE"));
    TraverseDepends = NYMake::IsTrue(CommandConf.EvalValue("TRAVERSE_DEPENDS"));
    AddPeerdirsGenTests = NYMake::IsTrue(CommandConf.EvalValue("ADD_PEERDIRS_GEN_TESTS"));
    AddPeersToInputs = NYMake::IsTrue(CommandConf.EvalValue("YMAKE_ADD_PEERS_TO_INPUTS"));
    ForceListDirInResolving = NYMake::IsTrue(CommandConf.EvalValue("RESOLVE_FORCE_LISTDIR"));
    CheckDependsInDart = NYMake::IsTrue(CommandConf.EvalValue("CHECK_DEPENDS_IN_DART"));
    JsonDepsFromMainOutputEnabled_ = NYMake::IsTrue(CommandConf.EvalValue("YMAKE_JSON_DEPS_FROM_MAIN_OUTPUT"));
    UseGraphChangesPredictor = NYMake::IsTrue(CommandConf.EvalValue("USE_GRAPH_CHANGES_PREDICTOR"));
    UseGrandBypass = !TDebugOptions::DisableGrandBypass && NYMake::IsTrue(CommandConf.EvalValue("USE_GRAND_BYPASS"));
    YmakeSaveAllCachesWhenBadLoops_ = NYMake::IsTrue(CommandConf.EvalValue("YMAKE_SAVE_ALL_CACHES_WHEN_BAD_LOOPS"));

    auto updateFlag = [&](bool& flag, const char* variableName, bool log = false) {
        TStringBuf value = CommandConf.EvalValue(variableName);
        if (!value.empty()) {
            flag = NYMake::IsTrue(value);
        }
        if (log) {
            YDebug() << variableName << (flag ? " enabled" : " disabled") << (value.empty() ? " (by default)" : "") << Endl;
        }
    };

    updateFlag(MainOutputAsExtra_, "MAIN_OUTPUT_AS_EXTRA", true);
    updateFlag(DedicatedModuleNode_, "YMAKE_DEDICATED_MODULE_NODE", true);
    updateFlag(CheckForIncorrectLoops_, "YMAKE_CHECK_FOR_INCORRECT_LOOPS", true);

    if (const auto val = CommandConf.EvalValue("NON_FATAL_ADDINCL_TO_MISSING"); !val.empty()) {
        ReportMissingAddincls = !NYMake::IsTrue(val);
    }

    if (CacheConfig.empty()) {
        // We need to revise the use of JSON_CACHE_IS_ATTACHED variable later.
        // This variable is used in tests for ymake caches.
        const auto jsonCacheAttached = NYMake::IsTrue(CommandConf.EvalValue("JSON_CACHE_IS_ATTACHED"));
        if (jsonCacheAttached && RebuildGraph) {
            ReadJsonCache = false;
        }
    }

    if (NYMake::IsTrue(CommandConf.EvalValue("DEPS_CACHE_CONTROL_UIDS_CACHE"))) {
        MakeDepsCacheControlUidsCache();
    }

    UidsSalt = CommandConf.Get1(VAR_FAKEID);

    if (auto rawValue = CommandConf.Get1(VAR_EXPORT_SRC_ROOT)) {
        ExportSourceRoot = GetCmdValue(rawValue);
    }

    const auto defaultRequirementsStr = GetCmdValue(CommandConf.Get1(VAR_DEFAULT_REQUIREMENTS));
    ParseRequirements(defaultRequirementsStr, DefaultRequirements);

    if (!ExpressionErrorDetails) {
        auto _dbg_expr_diag = CommandConf.EvalValue("_DBG_EXPR_ERROR_DETAILS");
        if (!_dbg_expr_diag.empty())
            ExpressionErrorDetails = ParseShowExpressionErrors(_dbg_expr_diag);
    }
    ValidateCmdNodes = NYMake::IsTrue(CommandConf.EvalValue("_DBG_VALIDATE_CMD_NODES"));
    DeprecateNonStructCmdNodes = NYMake::IsTrue(CommandConf.EvalValue("_DBG_DEPRECATE_NON_STRUCT_CMD_NODES"));
}

void TBuildConfiguration::InitExcludedPeerdirs() {
    const TStringBuf excludedPeerdirsStr = GetCmdValue(CommandConf.Get1(VAR_EXCLUDED_PEERDIRS));
    for (const TStringBuf dir : StringSplitter(excludedPeerdirsStr).Split(' ').SkipEmpty()) {
        ExcludedPeerdirs.insert(ArcPath(dir));
    }
}

bool TBuildConfiguration::IsIncludeOnly(const TStringBuf& name) const {
    return FindIf(IncludeExts, [&name](auto& ext) {return name.EndsWith(ext);}) != IncludeExts.end();
}

bool TBuildConfiguration::IsRequiredBuildAndSrcRoots(const TStringBuf& lang) const {
    return Find(LangsRequireBuildAndSrcRoots, lang) != LangsRequireBuildAndSrcRoots.end();
}
