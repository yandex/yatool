#pragma once

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/folder/path.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>

struct TCommandLineOptions {
    TString WriteJSON;
    TVector<TString> FindPathFrom;
    TVector<TString> FindPathTo;
    TVector<TString> ManagedDepTreeRoots;
    TVector<TString> DumpDMRoots;
    TString WriteMetaData;
    TFsPath CachePath;
    TString PatchPath;

    bool Test = false;
    bool KeepGoing = false;
    bool VerboseMake = false;
    bool DependsLikeRecurse = false;
    bool DisableHumanReadableOutput = false;
    bool DumpInputsMapInJSON = false;
    bool DumpInputsInJSON = false;
    bool StoreInputsInJsonCache = true;
    bool CheckDataPaths = false;
    bool ReadFileContentFromZipatch = false;

    TString WriteTestDart;
    TString WriteJavaDart;
    TString WriteMakeFilesDart;
    TString WriteYdx;

    TString ModulesInfoFile;
    TString ModulesInfoFilter;

    TString JsonCompressionCodec;

    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);

private:
    // TODO Remove me with --keep-on alias
    bool KeepOn = false;
};
