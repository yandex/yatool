#pragma once

#include <devtools/ymake/blacklist.h>
#include <devtools/ymake/isolated_projects.h>
#include <devtools/ymake/peers_rules.h>
#include <devtools/ymake/sysincl_resolver.h>
#include <devtools/ymake/licenses_conf.h>

#include <devtools/ymake/include_processors/base.h>

#include <devtools/ymake/compact_graph/dep_graph.h>

#include <devtools/ymake/common/cyclestimer.h>

#include <devtools/ymake/options/commandline_options.h>
#include <devtools/ymake/options/startup_options.h>
#include <devtools/ymake/options/static_options.h>
#include <devtools/ymake/options/debug_options.h>
#include <devtools/ymake/options/roots_options.h>

#include <devtools/ymake/config/config.h>
#include <devtools/ymake/diag/trace.h>

#include <library/cpp/getopt/small/last_getopt.h>
#include <library/cpp/digest/md5/md5.h>

#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/strbuf.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

constexpr TStringBuf MODULE_MANGLING_DELIM = "__from__";

class TFileConf;

/// required directories - source, build, svn and path operations only
// build configuration operates local-formatted paths
class TBuildConfiguration: public TStartUpOptions, public TYmakeConfig, public TDebugOptions, public TCommandLineOptions {
public:
    TString Events;
    TString CustomData;
    TVector<TFsPath> PluginsRoots;
    TVector<TString> WarnFlags;
    TMaybe<std::underlying_type_t<EConfMsgType>> WarnLevel;

    THashMap<TString, TFsPath> CustomDataGen;
    TParsersList ParserPlugins;

    TVector<TString> Plugins;
    THashMap<TString, THashSet<TString>> PluginDeps;

    TSysinclResolver Sysincl;
    THashMap<TString, TLicenseGroup> Licenses;
    TVector<TStringBuf> IncludeExts;
    TVector<TStringBuf> LangsRequireBuildAndSrcRoots;

    TPeersRules PeersRules;
    TBlackList BlackList;
    TIsolatedProjects IsolatedProjects;
    THashSet<TString> ExcludedPeerdirs;
    THolder<NYMake::TTraceStageWithTimer> RunStageWithTimer;

public:
    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);

    bool IsIncludeOnly(const TStringBuf& name) const;
    bool IsRequiredBuildAndSrcRoots(const TStringBuf& lang) const;

    bool ShouldTraverseRecurses() const {
        return TraverseRecurses;
    }

    void SetTraverseRecurses(bool value) {
        TraverseRecurses = value;
    }

    bool ShouldTraverseDepsTests() const {
        return TraverseDepsTests;
    }

    bool ShouldTraverseAllRecurses() const {
        return TraverseAllRecurses;
    }

    void SetTraverseAllRecurses(bool value) {
        TraverseAllRecurses = value;
    }

    bool ShouldFailOnRecurse() const {
        return FailOnRecurse;
    }

    bool ShouldTraverseDepends() const {
        return TraverseDepends;
    }

    bool ShouldAddPeerdirsGenTests() const {
        return AddPeerdirsGenTests;
    }

    bool ShouldAddPeersToInputs() const {
        return AddPeersToInputs;
    }

    bool ShouldForceListDirInResolving() const {
        return ForceListDirInResolving;
    }

    bool ShouldCheckDependsInDart() const {
        return CheckDependsInDart;
    }

    bool ShouldReportMissingAddincls() const noexcept {
        return ReportMissingAddincls;
    }

    bool JsonDepsFromMainOutputEnabled() const noexcept {
        return JsonDepsFromMainOutputEnabled_;
    }

    bool ShouldUseGraphChangesPredictor() const noexcept {
        return UseGraphChangesPredictor;
    }

    bool MainOutputAsExtra() const noexcept {
        return MainOutputAsExtra_;
    }

    TStringBuf GetUidsSalt() const {
        return UidsSalt;
    }

    TStringBuf GetExportSourceRoot() const {
        return ExportSourceRoot;
    }

    const THashMap<TString, TString>& GetDefaultRequirements() const {
        return DefaultRequirements;
    }

    void SetBlacklistHashChanged(bool value) {
        BlacklistHashChanged_ = value;
    }

    bool IsBlacklistHashChanged() const noexcept {
        return BlacklistHashChanged_;
    }

    void SetIsolatedProjectsHashChanged(bool value) {
        IsolatedProjectsHashChanged_ = value;
    }

    bool IsIsolatedProjectsHashChanged() const noexcept {
        return IsolatedProjectsHashChanged_;
    }

public:
    static const bool Workaround_AddGlobalVarsToFileNodes = true; // FIXME make it false forevermore

private:
    void PrepareBuildDir() const;
    void GenerateCustomData(const TStringBuf genCustomData);
    void LoadSystemHeaders(MD5& confData);
    void LoadLicenses(MD5& confData);
    void LoadPeersRules(MD5& confData);
    void LoadBlackLists(MD5& confHash, MD5& anotherConfHash);
    void LoadIsolatedProjects(MD5& confData, MD5& anotherConfData, bool addAnother);
    void FillMiscValues();
    void InitExcludedPeerdirs();
    void CompileAndRecalcAllConditions();

    bool TraverseRecurses = false;
    bool TraverseAllRecurses = false;     // Including RECURSE_FOR_TESTS
    bool TraverseDepsTests = false; // Include RECURSE_FOR_TESTS of PEERDIRs
    bool FailOnRecurse = false;
    bool TraverseDepends = false;
    bool AddGlobalSrcToResults = false;
    bool AddPeerdirsGenTests = false;
    bool AddPeersToInputs = false;
    bool ForceListDirInResolving = false;
    bool CheckDependsInDart = false;
    bool ReportMissingAddincls = true;
    bool JsonDepsFromMainOutputEnabled_ = false;
    bool MainOutputAsExtra_ = false;
    bool UseGraphChangesPredictor = false;
    bool BlacklistHashChanged_ = true; // by default require apply blacklist for all modules
    bool IsolatedProjectsHashChanged_ = true; // by default require apply isolated projects for all modules

    TStringBuf UidsSalt;
    TStringBuf ExportSourceRoot;
    THashMap<TString, TString> DefaultRequirements;
};

TBuildConfiguration* GlobalConf();
void SetGlobalConf(TBuildConfiguration* conf);

struct TDummyBuildConfiguration: public TBuildConfiguration {
    TDummyBuildConfiguration() {
        SetGlobalConf(this);
    }

    ~TDummyBuildConfiguration() {
        SetGlobalConf(nullptr);
    }
};
