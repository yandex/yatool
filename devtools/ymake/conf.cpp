#include "conf.h"

#include "sysincl_conf.h"
#include "licenses_conf.h"

#include <devtools/ymake/macro.h>
#include <devtools/ymake/macro_processor.h>
#include <devtools/ymake/macro_string.h>
#include <devtools/ymake/plugins/cpp_plugins.h>
#include <devtools/ymake/lang/plugin_facade.h>
#include <devtools/ymake/yndex/builtin.h>
#include <devtools/ymake/diag/diag.h>
#include <devtools/ymake/diag/trace.h>
#include <devtools/ymake/common/memory_pool.h>

#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
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

    void FoldGlobalCommands(TBuildConfiguration* conf) {
        Y_ASSERT(conf != nullptr);
        auto& vars = conf->CommandConf;

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
}

void TBuildConfiguration::AddOptions(NLastGetopt::TOpts& opts) {
    TCommandLineOptions::AddOptions(opts);
    TStartUpOptions::AddOptions(opts);
    TDebugOptions::AddOptions(opts);

    opts.AddLongOption("sem-graph", "dump semantic graph instead of build plan").SetFlag(&RenderSemantics).NoArgument();
    opts.AddLongOption('w', "warn-level", "level of human-readable messages to be shown (0 or more: none, error, warning, info, debug)").StoreResult(&WarnLevel);
    opts.AddLongOption('W', "warn", "warnings & messages to display").AppendTo(&WarnFlags);
    opts.AddLongOption('E', "events", "enable/set trace events").StoreResult(&Events);
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

void TBuildConfiguration::PostProcess(const TVector<TString>& freeArgs) {
    if (!DisableHumanReadableOutput) {
        Display()->SetStream(LockedStream());
    }
    if (WarnLevel) {
        auto warnLevel = static_cast<EConfMsgType>(*WarnLevel);
        if (warnLevel >= EConfMsgType::Error && warnLevel <= EConfMsgType::Count)
            Display()->SetCutoff(warnLevel);
    }

    if (!Events.empty()) {
        InitTraceSubsystem(Events);
    }

    RunStageWithTimer = MakeHolder<TTraceStageWithTimer>("ymake run", MON_NAME(EYmakeStats::RunTime));

    Diag()->Init(WarnFlags);

    if (!BinaryLogFileName.Empty()) {
        Diag()->BinaryLog = true;
        DebugLogWriter()->SetFile(BinaryLogFileName);
    }

    if (DisableTextLog) {
        Diag()->TextLog = false;
    }

    if (!PatchPath2.Empty()) {
        PatchPath = PatchPath2;
        ReadFileContentFromZipatch = ReadFileContentFromZipatch2;
    }

    TCommandLineOptions::PostProcess(freeArgs);
    TStartUpOptions::PostProcess(freeArgs);
    TDebugOptions::PostProcess(freeArgs);

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

    SetGlobalConf(this);

    if (WriteYdx.empty()) {
        CommandDefinitions.Disable();
    }

    NYndex::AddBuiltinDefinitions(CommandDefinitions);
    LoadConfig(YmakeConf.GetPath(), SourceRoot.GetPath(), BuildRoot.GetPath(),  confData);

    CompileAndRecalcAllConditions();

    {
        TTraceStage stage("Fold global commands");
        FoldGlobalCommands(this);
    }

    // this code hangs on win while trying to lock locked non-recursive mutex
    // (after an error that file is not found)
    if (!DontUsePlugins) {
        if (!PluginsRoots.empty()) {
            PluginConfig()->Init(SourceRoot.GetPath().c_str(), BuildRoot.GetPath().c_str());
            LoadPlugins(PluginsRoots, this);
            for (const auto& it : Plugins) {
                TBlob pluginContent = TBlob::FromFile(it);
                confData.Update((const char*)pluginContent.Data(), pluginContent.Size());
            }
        }

        // All cpp plugins should be registered in RegisterCppPlugins()
        NYMake::NPlugins::RegisterCppPlugins();
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
        LoadBlackLists(blacklistHash, rulesData, NYMake::IsTrue(CommandConf.EvalValue("INCLUDE_BLACKLIST_TO_CONF_HASH")));
        blacklistHash.Final(YmakeBlacklistHash.RawData);
    }
    if (Diag()->IslPrjs) {
        // Add isolated projects hash to only isolated projects hash (new behavior) and rules (current behavior - deprecated) by config switch
        LoadIsolatedProjects(isolatedProjectsHash, rulesData, NYMake::IsTrue(CommandConf.EvalValue("INCLUDE_ISOLATED_PROJECTS_TO_CONF_HASH")));
        isolatedProjectsHash.Final(YmakeIsolatedProjectsHash.RawData);
    }
    LoadLicenses(extraData);
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
    TString sysinclVar = TCommandInfo(this, nullptr, nullptr).SubstVarDeeply(TStringBuf("SYSINCL"), CommandConf);
    TVector<TFsPath> sysinclFiles;
    for (const auto& it : StringSplitter(sysinclVar).Split(' ').SkipEmpty()) {
        sysinclFiles.emplace_back(SourceRoot / it.Token());
    }
    Sysincl = ::LoadSystemIncludes(sysinclFiles, confData);
}

void TBuildConfiguration::LoadLicenses(MD5& confData) {
    TString licenses = TCommandInfo(this, nullptr, nullptr).SubstVarDeeply(TStringBuf("LICENSES"), CommandConf);
    TVector<TFsPath> licensesFiles;
    for (const auto& it : StringSplitter(licenses).Split(' ').SkipEmpty()) {
        licensesFiles.emplace_back(SourceRoot / it.Token());
    }
    Licenses = ::LoadLicenses(licensesFiles, confData);
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

void TBuildConfiguration::LoadBlackLists(MD5& confHash, MD5& anotherConfHash, bool addToAnother) {
    const TString blacklistsVar = TCommandInfo(this, nullptr, nullptr).SubstVarDeeply(VAR_BLACKLISTS, CommandConf);
    TVector<TStringBuf> blacklistFiles = StringSplitter(blacklistsVar).Split(' ').SkipEmpty();
    BlackList.Load(SourceRoot, blacklistFiles, confHash, anotherConfHash, addToAnother);
}

void TBuildConfiguration::LoadIsolatedProjects(MD5& confData, MD5& anotherConfData, bool addAnother) {
    const TString isolatedProjectsVar = TCommandInfo(this, nullptr, nullptr).SubstVarDeeply(VAR_ISOLATED_PROJECTS, CommandConf);
    TVector<TStringBuf> isolatedProjectsFiles = StringSplitter(isolatedProjectsVar).Split(' ').SkipEmpty();
    IsolatedProjects.Load(SourceRoot, isolatedProjectsFiles, confData, anotherConfData, addAnother);
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
    MainOutputAsExtra_ = NYMake::IsTrue(CommandConf.EvalValue("MAIN_OUTPUT_AS_EXTRA"));
    UseGraphChangesPredictor = !ReadStartTargetsFromEvlog && NYMake::IsTrue(CommandConf.EvalValue("USE_GRAPH_CHANGES_PREDICTOR"));
    UseGrandBypass = NYMake::IsTrue(CommandConf.EvalValue("USE_GRAND_BYPASS"));

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

namespace {
    //temp
    struct TBuildConfHandler {
        inline TBuildConfHandler()
            : Conf(nullptr)
        {
        }

        inline TBuildConfiguration* Get() noexcept {
            Y_ASSERT(Conf);

            return Conf;
        }

        TBuildConfiguration* Conf;
    };
}

TBuildConfiguration* GlobalConf() {
    return Singleton<TBuildConfHandler>()->Get();
}

void SetGlobalConf(TBuildConfiguration* conf) {
    Singleton<TBuildConfHandler>()->Conf = conf;
}
