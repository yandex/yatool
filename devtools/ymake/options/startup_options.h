#pragma once

#include "roots_options.h"

#include <devtools/ymake/config/transition.h>

#include <library/cpp/getopt/small/last_getopt.h>

#include <util/folder/path.h>

// configuration operates local-formatted paths
struct TStartUpOptions: public TRootsOptions {
    TFsPath YmakeConf;
    TFsPath YmakeCache;
    TFsPath YmakeDMCache;
    TFsPath YmakeConfCache;
    TFsPath YmakeUidsCache;
    TFsPath YmakeJsonCache;
    TFsPath ConfDir;

    TVector<TFsPath> StartDirs;
    TVector<TFsPath> Targets;

    bool ReadStartTargetsFromEvlog = false;
    ETransition TransitionSource = ETransition::None;
    bool DescendIntoForeignPlatform = true;
    bool ReportPicNoPic = false;

    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);
    void MineTargetsAndSourceRoot(const TVector<char*>& optPos);
    void MineTargetsAndSourceRoot(const TVector<TString>& optPos);

    void AddTarget(const TFsPath& target);

    void OnDepsCacheSaved() const;
};
