#include "startup_options.h"

#include <devtools/ymake/diag/display.h>
#include <devtools/ymake/common/probe.h>
#include <devtools/ymake/config/transition.h>
#include <devtools/ymake/diag/dbg.h>
#include <devtools/ymake/diag/trace.h>

#include <util/system/execpath.h>
#include <util/string/split.h>
#include <util/folder/path.h>
#include <util/stream/file.h>
#include <util/system/fs.h>
#include <util/stream/pipe.h>

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
    opts.AddLongOption("targets-from-evlog", "read start targets from evlog").SetFlag(&ReadStartTargetsFromEvlog).NoArgument();
    opts.AddLongOption("transition-source").StoreResult<ETransition>(&TransitionSource).Optional();
    opts.AddLongOption("platform-id").StoreResult(&TargetPlatformId).Optional();
    opts.AddLongOption("descend-into-foreign", "follow deps leading into foreign platforms").StoreResult(&DescendIntoForeignPlatform);
    opts.AddLongOption("report-pic-nopic", "report pic/no-pic foreign target events").StoreResult(&ReportPicNoPic);
    opts.AddLongOption("fd-in", "input pipe fd").StoreResult(&InputPipeFd);
    opts.AddLongOption("fd-out", "output pipe fd").StoreResult(&OutputPipeFd);
    opts.AddLongOption("fd-err", "error pipe fd").StoreResult(&ErrorPipeFd);
    opts.AddLongOption("dont-check-transitive-requirements", "").StoreFalse(&CheckTransitiveRequirements);
    opts.AddLongOption("parallel-rendering", "").StoreTrue(&ParallelRendering);
    opts.AddLongOption("use-subinterpreters", "Use subinterpreters").SetFlag(&UseSubinterpreters).NoArgument();
}

void TStartUpOptions::PostProcess(const TVector<TString>& freeArgs) {
    if (InputPipeFd != -1) {
        InputStream = MakeAtomicShared<TPipedInput>(InputPipeFd);
    } else {
        InputStream = &Cin;
        InputStream.ReferenceCounter()->Inc();  // hack to have smartptr interface but don't own the ptr
    }
    if (OutputPipeFd != -1) {
        OutputStream = MakeAtomicShared<TPipedOutput>(OutputPipeFd);
    } else {
        OutputStream = &Cout;
        OutputStream.ReferenceCounter()->Inc();  // hack to have smartptr interface but don't own the ptr
    }
    if (ErrorPipeFd != -1) {
        ErrorStream = MakeAtomicShared<TPipedOutput>(ErrorPipeFd);
        NYMake::SetTraceOutputStream(ErrorStream);
    } else {
        ErrorStream = &Cerr;
        ErrorStream.ReferenceCounter()->Inc();  // hack to have smartptr interface but don't own the ptr
    }

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
    YmakeDMCache = BuildRoot / "ymake.dm.cache";
    YmakeConfCache = BuildRoot / "ymake.conf.cache";
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
        if (!SourceRoot) {
            throw TConfigurationError() << "explicit --source-root is required for --targets-from-evlog";
        }

        // This is a dirty workaround. Some sandbox tasks use arcadia symlink as a source root
        // (https://a.yandex-team.ru/arcadia/sandbox/projects/common/build/YaMake/__init__.py?rev=r15185065#L657).
        // Here we follow these symlinks instead of disabling servermode in all current and future cases that use this technique.
        // TODO(YMAKE-1505): remove this when ymake could work with symlinked source tree w/o issues.
        SourceRoot = SourceRoot.RealLocation();
    } else {
        if (targets.empty()) {
            targets.push_back(TFsPath::Cwd().RealPath());
        }

        if (!SourceRoot) {
            SourceRoot = FindSourceRootByTarget(targets[0]);
        }
    }

    // TODO: check SourceRoot.IsAbsolute()
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
    auto absTarget = target.IsAbsolute() ? target : SourceRoot / target;
    TFsPath absTargetDir = absTarget.Parent();
    if (!absTarget.IsDirectory() && !absTargetDir.IsDirectory()) {
        throw TConfigurationError() << "No directory for target " << absTarget;
    }

    Targets.push_back(absTarget.RelativeTo(SourceRoot));
    if (!Targets.back().IsDefined()) { // fix this in util
        Targets.back() = ".";
        StartDirs.push_back(".");
    } else {
        StartDirs.push_back(absTarget.IsDirectory() ? Targets.back() : absTargetDir.RelativeTo(SourceRoot));
        if (!StartDirs.back().IsDefined()) // fix this in util
            StartDirs.back() = ".";
    }
}

void TStartUpOptions::OnDepsCacheSaved() const {
    FORCE_TRACE(C, NEvent::TReadyForUpdateCacheInfo{});
}
