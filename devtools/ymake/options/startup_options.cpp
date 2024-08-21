#include "startup_options.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/common/probe.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/trace.h>

#include <util/system/execpath.h>
#include <util/string/split.h>
#include <util/folder/path.h>
#include <util/stream/file.h>
#include <util/system/fs.h>

static inline TFsPath GetConfDir(const TFsPath& root) {
    return root / "build";
}

static inline bool IsSourceRoot(const TFsPath& root) {
    TFsPath ymakeCore = GetConfDir(root) / "ymake.core.conf";
    return ymakeCore.Exists() && ymakeCore.IsFile();
}

static TString FindSourceRootByTarget(const TFsPath& path) {
    TFsPath root = path;
    while (!IsSourceRoot(root)) {
        if (root == root.Parent()) {
            YErr() << "path/to/arcadia/root not found for: " << path;
            return "";
        }
        root = root.Parent();
        if (root.IsRelative()) {
            YErr() << "path/to/arcadia/root not found for: " << path;
            return "";
        }
    }

    return root;
}

void TStartUpOptions::AddOptions(NLastGetopt::TOpts& opts) {
    TRootsOptions::AddOptions(opts);
    opts.AddLongOption('c', "config").StoreResult(&YmakeConf).Required();
    opts.AddLongOption("cache-info-file", "fullname of cache info file for current ymake run").StoreResult(&CacheInfoFile);
    opts.AddLongOption("cache-info-name", "basename for cache info output file").StoreResult(&CacheInfoName);
    opts.AddLongOption("targets-from-evlog", "read start targets from evlog").SetFlag(&ReadStartTargetsFromEvlog).NoArgument();
}

void TStartUpOptions::PostProcess(const TVector<TString>& freeArgs) {
    TRootsOptions::PostProcess(freeArgs);

    if (!YmakeConf.IsDefined() || !YmakeConf.Exists() || !YmakeConf.IsFile()) {
        throw TConfigurationError() << "ymake conf file " << YmakeConf << " is incorrect";
    }

    MineTargetsAndSourceRoot(freeArgs);
    if (!SourceRoot || !SourceRoot.Exists() || !SourceRoot.IsDirectory()) {
        throw TConfigurationError() << "source root " << SourceRoot << " is incorrect";
    }

    if (BuildRoot == SourceRoot || SourceRoot.IsSubpathOf(BuildRoot)) {
        throw TConfigurationError() << "source root cannot be inside of build root";
    }

    if (CacheInfoFile.IsDefined() && !CacheInfoFile.IsFile()) {
        throw TConfigurationError() << "cache info file " << CacheInfoFile << " is incorrect";
    }

    if (CacheInfoName.IsDefined() && CacheInfoName != CacheInfoName.Basename()) {
        throw TConfigurationError() << "cache info name " << CacheInfoName << " is incorrect";
    }

    if (CacheInfoName.IsDefined()) {
        CacheInfoOutputFile = BuildRoot / CacheInfoName;
        if (CacheInfoFile.IsDefined() && CacheInfoFile == CacheInfoOutputFile) {
            throw TConfigurationError() << "cache info file " << CacheInfoFile << " must be different from cache info output file";
        }
    }

    YmakeCache = BuildRoot / "ymake.cache";
    YmakeDMCache = BuildRoot / "ymake.dm.cache";
    YmakeUidsCache = BuildRoot / "ymake.uids.cache";
    YmakeJsonCache = BuildRoot / "ymake.json.cache";
    ConfDir = SourceRoot / "build";
}

void TStartUpOptions::MineTargetsAndSourceRoot(const TVector<char*>& optPos) {
    TVector<TString> args;
    for (const auto& opt : optPos) {
        args.push_back(opt);
    }
    return MineTargetsAndSourceRoot(args);
}

void TStartUpOptions::MineTargetsAndSourceRoot(const TVector<TString>& optPos) {
    TVector<TFsPath> targets;
    for (size_t pos = 0; pos < optPos.size(); ++pos) {
        TVector<TStringBuf> optSplitted;
        Split(optPos[pos], ";", optSplitted);
        for (const auto& target : optSplitted) {
            try {
                targets.push_back(TFsPath(target).RealLocation());
            } catch (const TFileError&) {
                throw TConfigurationError() << "Directory of user target '" << target << "' not found";
            }
        }
    }

    if (ReadStartTargetsFromEvlog) {
        if (!targets.empty()) {
            throw TConfigurationError() << "--targets-from-evlog is not compatible with commandline targets";
        }

        if (!SourceRoot) {
            throw TConfigurationError() << "explicit --source-root is required for --targets-from-evlog";
        }

        return;
    }

    if (targets.empty()) {
        targets.push_back(TFsPath::Cwd().RealPath());
    }

    if (!SourceRoot) {
        SourceRoot = FindSourceRootByTarget(targets[0]);
    }

    if (!SourceRoot || !SourceRoot.Exists() || !SourceRoot.IsDirectory()) {
        throw TConfigurationError() << "source root " << SourceRoot << " is incorrect";
    }

    for (size_t i = 0; i < targets.size(); ++i) {
        YDebug() << "Target path: " << targets[i] << Endl;
        AddTarget(targets[i]);
    }
}

void TStartUpOptions::AddTarget(const TFsPath& target) {
    // TODO: check target inside source root
    TFsPath targetDir = target.Parent();
    if (!target.IsDirectory() && !targetDir.IsDirectory()) {
        throw TConfigurationError() << "No directory for target " << target;
    }

    Targets.push_back(target.IsAbsolute() ? target.RelativeTo(SourceRoot) : target);
    if (!Targets.back().IsDefined()) { // fix this in util
        Targets.back() = ".";
        StartDirs.push_back(".");
    } else {
        StartDirs.push_back(target.IsDirectory() ? Targets.back() : targetDir.IsAbsolute() ? targetDir.RelativeTo(SourceRoot) : targetDir);
        if (!StartDirs.back().IsDefined()) // fix this in util
            StartDirs.back() = ".";
    }
}

void TStartUpOptions::OnDepsCacheEnabled() const {
    if (CacheInfoOutputFile.IsDefined()) {
        CacheInfoOutputFile.DeleteIfExists();
    } else {
        // Magic ortopedia - remove commit hash by known basename
        auto const KnownCacheInfoOutputFile = BuildRoot / "hash";
        KnownCacheInfoOutputFile.DeleteIfExists();
    }
}

void TStartUpOptions::OnDepsCacheSaved() const {
    if (CacheInfoFile.IsDefined() && CacheInfoOutputFile.IsDefined()) {
        // Use CopyTo instead RenameTo, because few ymake run may use one CacheInfoFile
        CacheInfoFile.CopyTo(CacheInfoOutputFile, true);
    }
    FORCE_TRACE(T, NEvent::TReadyForUpdateCacheInfo{});
}
