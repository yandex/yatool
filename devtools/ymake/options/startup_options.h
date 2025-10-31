#pragma once

#include "roots_options.h"

#include <devtools/ymake/config/transition.h>

#include <devtools/ymake/foreign_platforms/io.h>
#include <library/cpp/getopt/small/last_getopt.h>

#include <util/folder/path.h>
#include <util/system/pipe.h>

// configuration operates local-formatted paths
struct TStartUpOptions: public TRootsOptions {
    TFsPath YmakeConf;
    TFsPath YmakeCache;
    TFsPath YmakeDMCache;
    TFsPath YmakeConfCache;
    TFsPath YmakeUidsCache;
    TFsPath YmakeJsonCache;
    TFsPath ConfDir;
    TFsPath YmakeDartsCacheDir;

    TVector<TFsPath> StartDirs;
    TVector<TFsPath> Targets;

    bool ReadStartTargetsFromEvlog = false;
    ETransition TransitionSource = ETransition::None;
    TString TargetPlatformId;
    bool DescendIntoForeignPlatform = true;
    bool ReportPicNoPic = false;
    bool CheckTransitiveRequirements = true;
    bool ParallelRendering = false;

    PIPEHANDLE InputPipeFd = -1;
    PIPEHANDLE OutputPipeFd = -1;
    PIPEHANDLE ErrorPipeFd = -1;
    TAtomicSharedPtr<IInputStream> InputStream;
    TAtomicSharedPtr<IOutputStream> OutputStream;
    TAtomicSharedPtr<IOutputStream> ErrorStream;

    THolder<NForeignTargetPipeline::TLineReader> ForeignTargetReader;
    THolder<NForeignTargetPipeline::TLineWriter> ForeignTargetWriter;

    void AddOptions(NLastGetopt::TOpts& opts);
    void PostProcess(const TVector<TString>& freeArgs);
    void MineTargetsAndSourceRoot(const TVector<char*>& optPos);
    void MineTargetsAndSourceRoot(const TVector<TString>& optPos);

    void AddTarget(const TFsPath& target);

    void OnDepsCacheSaved() const;
};
