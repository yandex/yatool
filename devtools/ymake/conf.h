#pragma once

#include <devtools/ymake/blacklist.h>
#include <devtools/ymake/isolated_projects.h>
#include <devtools/ymake/peers_rules.h>
#include <devtools/ymake/sysincl_resolver.h>

#include <devtools/ymake/include_processors/base.h>

#include <devtools/ymake/compact_graph/dep_graph.h>

#include <devtools/ymake/options/commandline_options.h>
#include <devtools/ymake/options/startup_options.h>
#include <devtools/ymake/options/static_options.h>
#include <devtools/ymake/options/debug_options.h>
#include <devtools/ymake/options/roots_options.h>

#include <devtools/ymake/config/config.h>

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

    THashMap<TString, TFsPath> CustomDataGen;
    TParsersList ParserPlugins;

    TVector<TString> Plugins;
    THashMap<TString, THashSet<TString>> PluginDeps;

    TSysinclResolver Sysincl;
    TVector<TStringBuf> IncludeExts;
    TVector<TStringBuf> LangsRequireBuildAndSrcRoots;

    TPeersRules PeersRules;
    TBlackList BlackList;
    TIsolatedProjects IsolatedProjects;
    THashSet<TString> ExcludedPeerdirs;

public:
    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);

    bool IsIncludeOnly(const TStringBuf& name) const;
    bool IsRequiredBuildAndSrcRoots(const TStringBuf& lang) const;

    bool ShouldTraverseRecurses() const {
        return TraverseRecurses;
    }

    bool ShouldTraverseDepsTests() const {
        return TraverseDepsTests;
    }

    bool ShouldTraverseAllRecurses() const {
        return TraverseAllRecurses;
    }

    bool ShouldFailOnRecurse() const {
        return FailOnRecurse;
    }

    bool ShouldTraverseDepends() const {
        return TraverseDepends;
    }

    bool ShouldAddGlobalSrcsToResults() const {
        return AddGlobalSrcToResults;
    }

    bool ShouldAddPeerdirsGenTests() const {
        return AddPeerdirsGenTests;
    }

    bool ShouldChkPeersForGlobalSrcs() const {
        return ChkPeersForGlobalSrcs;
    }

    bool ShouldCheckGoIncorrectDeps() const {
        return CheckGoIncorrectDeps;
    }

    bool ShouldReportRecurseNoYamake() const {
        return ReportRecurseNoYamake;
    }

    bool ShouldAddPeersToInputs() const {
        return AddPeersToInputs;
    }

    bool ShouldForceResolveMacroIncls() const {
        return ForceResolveForMacroIncls;
    }

    bool ShouldAddDataPaths() const {
        return AddDataPaths;
    }

    bool ShouldWriteSelfUids() const {
        return SelfUidsEnabled;
    }

    bool ShouldResolveOutInclsStrictly() const {
        return StrictOutInclsResolving;
    }

    bool ShouldForceListDirInResolving() const {
        return ForceListDirInResolving;
    }

    bool ShouldCheckDependsInDart() const {
        return CheckDependsInDart;
    }

    bool ShouldEmitNeedDirHints() const {
        return NeedDirHints;
    }

    bool ShouldReportMissingAddincls() const noexcept {
        return ReportMissingAddincls;
    }

    bool ShouldReportAllDupSrc() const {
        return ReportAllDupSrc;
    }

    bool IsReresolveForGeneratedFilesEnabled() const {
        return ReresolveForGeneratedFiles;
    }

    bool ShouldIgnoreDupSrcFor(TStringBuf name) const {
        return DupSrcIgnoreSet.contains(name);
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

private:
    void PrepareBuildDir() const;
    void GenerateCustomData(const TStringBuf genCustomData);
    void LoadSystemHeaders(MD5& confData);
    void LoadPeersRules(MD5& confData);
    void LoadBlackLists(MD5& confData);
    void LoadIsolatedProjects(MD5& confData);
    void FillMiscValues();
    void InitExcludedPeerdirs();

    bool TraverseRecurses = false;
    bool TraverseAllRecurses = false;     // Including RECURSE_FOR_TESTS
    bool TraverseDepsTests = false; // Include RECURSE_FOR_TESTS of PEERDIRs
    bool FailOnRecurse = false;
    bool TraverseDepends = false;
    bool AddGlobalSrcToResults = false;
    bool AddPeerdirsGenTests = false;
    bool ChkPeersForGlobalSrcs = false;
    bool CheckGoIncorrectDeps = false;
    bool ReportRecurseNoYamake = false;
    bool AddPeersToInputs = false;
    bool ForceResolveForMacroIncls = false;
    bool AddDataPaths = false;
    bool SelfUidsEnabled = false;
    bool StrictOutInclsResolving = false;
    bool ForceListDirInResolving = false;
    bool CheckDependsInDart = false;
    bool NeedDirHints = false;
    bool ReportAllDupSrc = false;
    bool ReresolveForGeneratedFiles = false;
    bool ReportMissingAddincls = true;

    TStringBuf UidsSalt;
    TStringBuf ExportSourceRoot;
    THashMap<TString, TString> DefaultRequirements;
    THashSet<TStringBuf> DupSrcIgnoreSet;
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
