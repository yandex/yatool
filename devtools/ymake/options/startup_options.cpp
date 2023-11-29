#include "startup_options.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/common/probe.h>
#include <devtools/ymake/diag/dbg.h>

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

    YmakeCache = BuildRoot / "ymake.cache";
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

        // TODO: check target inside source root
        TFsPath targetDir = targets[i].Parent();
        if (!targets[i].IsDirectory() && !targetDir.IsDirectory()) {
            throw TConfigurationError() << "No directory for target " << targets[i];
        }

        Targets.push_back(targets[i].RelativeTo(SourceRoot));
        if (!Targets.back().IsDefined()) { // fix this in util
            Targets.back() = ".";
            StartDirs.push_back(".");
        } else {
            StartDirs.push_back(targets[i].IsDirectory() ? Targets.back() : targetDir.RelativeTo(SourceRoot));
            if (!StartDirs.back().IsDefined()) // fix this in util
                StartDirs.back() = ".";
        }
    }
}
