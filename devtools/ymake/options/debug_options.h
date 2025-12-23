#pragma once

#include "static_options.h"

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/system/compiler.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/stream/file.h>
#include <util/stream/output.h>
#include <util/system/types.h>

struct TDebugOptions {
    bool RebuildGraph = false;
    bool RebuildJsonGraph = false;
    bool UseJsonCache = false;
    bool UseFSCacheOnly = false;
    bool CompletelyTrustFSCache = false;
    bool AllToStderr = false;

    bool ShowLoops = false; // byte

    bool DontWriteInternalCache = false;
    bool DontWriteJsonCache = false;

    bool NoParseSrc = false;
    bool NoChkYMakeChg = false;
    bool DontUsePlugins = false;

    bool DumpGraph = false;
    bool FullDumpGraph = false;
    bool DumpMakefiles = false;
    bool DumpGraphNoId = false; // byte
    bool DumpGraphNoPosNoId = false;
    bool DumpGraphFlatJson = false;
    bool DumpGraphFlatJsonWithCmds = false;
    bool DumpDirectDM = false;
    bool DumpFiles = false;
    bool DumpDirs = false;
    bool DumpSrcDeps = false;
    bool WithYaMake = false;
    bool DumpModules = false;
    bool DumpRecurses = false;
    bool DumpPeers = false;
    bool DumpDependentDirs = false;
    bool DumpData = false;  // This is for DumpFiles and DumpSrcDeps
    bool DumpDepends = false; // Dump depends as direct edges from modules
    bool DumpRenderedCmds = false; // byte
    bool DumpExpressions = false;
    bool DumpBuildables = false;
    bool DumpTargetDepFiles = false;
    bool DumpGraphStuff = false;
    bool DumpAsJson = false;
    bool DumpAsDot = false;
    bool WriteOwners = false;
    bool DumpNames = false;
    bool DumpIndirectPeerdirs = false;
    bool DumpAddinclsSubgraphs = false;
    bool DumpIncludeTargets = false;
    bool SkipMakeFilesInDumps = false;
    bool MarkMakeFilesInDumps = false;
    bool DumpPretty = false;

    bool EntirelyParseFiles = false;
    bool PrintTargetAbsPath = false;
    bool PrintTargets = false;
    bool SkipRecurses = false;
    bool SkipAllRecurses = false;
    bool SkipDepends = false;
    bool SkipTools = false;
    bool SkipAddincls = false;
    bool DumpRelationsByPrefix = false;
    bool DumpDepsBetweenTargets = false;

    bool DumpModulesInfo = false;
    bool DumpLicensesInfo = false;
    bool DumpLicensesMachineInfo = false;
    bool DumpForcedDependencyManagements = false;
    bool DumpForcedDependencyManagementsAsJson = false;
    TString LicenseLinkType;
    TVector<TString> LicenseTagVars;

    TString DumpFileName;
    mutable THolder<IOutputStream> DumpStream;  // Lazy-initialized on first Cmsg() call

    TString BinaryLogFileName;
    bool DisableTextLog = false;

    TVector<TString> CacheConfig;
    TVector<TString> RetryConfig;
    bool ReadFsCache = true;
    bool WriteFsCache = true;
    bool ReadDepsCache = true;
    bool WriteDepsCache = true;
    bool ReadJsonCache = true;
    bool WriteJsonCache = true;
    bool ReadDepManagementCache = true;
    bool WriteDepManagementCache = true;
    bool ReadUidsCache = false;
    bool WriteUidsCache = false;

    bool UidsCacheWasSetExplicitly = false;

    bool ReadConfCache = true;
    bool WriteConfCache = true;
    bool ConfCacheWasSetExplicitly = false;

    TString PatchPath2;
    bool ReadFileContentFromZipatch2 = false;

    bool DisableGrandBypass = false;

    enum class EShowExpressionErrors {None, One, All};
    EShowExpressionErrors ParseShowExpressionErrors(TStringBuf s);
    std::optional<EShowExpressionErrors> ExpressionErrorDetails;
    bool ValidateCmdNodes = false;
    bool DeprecateNonStructCmdNodes = false;

    // The Uids cache can be controlled by the Deps cache settings,
    // but this still could be overridden by --xCC=u and --xRC=u.
    void MakeDepsCacheControlUidsCache() {
        if (!UidsCacheWasSetExplicitly) {
            ReadUidsCache = ReadDepsCache;
            WriteUidsCache = WriteDepsCache;
        }
    }

    void DisableConfCache() {
        ReadConfCache = false;
        WriteConfCache = false;
    }

    void DoNotWriteAllCaches() {
        WriteFsCache = false;
        WriteDepsCache = false;
        WriteJsonCache = false;
        WriteDepManagementCache = false;
        WriteUidsCache = false;
    }

    // The Conf cache can be controlled by the Deps Cache Settings,
    // but this still could be overridden by --xCC=c and --xCR=c.
    void MakeDepsCacheControlConfCache() {
        if (!ConfCacheWasSetExplicitly) {
            ReadConfCache = ReadDepsCache;
            WriteConfCache = WriteDepsCache;
        }
    }

    IOutputStream& Cmsg() const {
        if (!DumpFileName.empty()) {
            if (!DumpStream) {
                DumpStream = MakeHolder<TFileOutput>(DumpFileName);
            }
            return *DumpStream;
        }
        return AllToStderr ? Cerr : Cout;
    }

    inline bool NeedParseLine(size_t lineNumber) {
        return lineNumber <= NStaticConf::INCLUDE_LINES_LIMIT || Y_UNLIKELY(EntirelyParseFiles);
    }

    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);
};
