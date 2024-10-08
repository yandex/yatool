#include "config.h"

#include <util/folder/path.h>

bool NACCachePrivate::TConfigOptions::HasParams() const {
    return LockFile != "" || DBPath != "" ||
           !MasterMode.Empty() || !GraphInfo.Empty() || !ForeignKeys.Empty() || !NoDb.Empty() ||
           !CasLogging.Empty() || !NoBlobIO.Empty() || !RecreateDB.Empty() ||
           !DiskLimit.Empty() || !QuiescenceTime.Empty() ||
           FsStoreRoot != "";
}

void NACCachePrivate::TConfigOptions::WriteSection(IOutputStream& out) const {
    if (!HasParams()) {
        return;
    }

    out << "[" << IniSectionName() << "]" << Endl;

    if (!LockFile.empty()) {
        out << ToString(LockFileStr) << "=" << LockFile << Endl;
    }
    if (!DBPath.empty()) {
        out << ToString(DbPathStr) << "=" << DBPath << Endl;
    }
    if (auto* val = MasterMode.Get()) {
        out << ToString(MasterModeStr) << "=" << *val << Endl;
    }
    if (auto* val = CasLogging.Get()) {
        out << ToString(CasLoggingStr) << "=" << *val << Endl;
    }
    if (auto* val = GraphInfo.Get()) {
        out << ToString(GraphInfoStr) << "=" << *val << Endl;
    }
    if (auto* val = ForeignKeys.Get()) {
        out << ToString(ForeignKeysStr) << "=" << *val << Endl;
    }
    if (auto* val = NoDb.Get()) {
        out << ToString(NoDbStr) << "=" << *val << Endl;
    }
    if (auto* val = NoBlobIO.Get()) {
        out << ToString(NoBlobIOStr) << "=" << *val << Endl;
    }
    if (auto* val = RecreateDB.Get()) {
        out << ToString(RecreateStr) << "=" << *val << Endl;
    }
    if (auto* val = DiskLimit.Get()) {
        out << ToString(GcLimitStr) << "=" << *val << Endl;
    }
    if (auto* val = QuiescenceTime.Get()) {
        out << ToString(QuiescenceStr) << "=" << *val << Endl;
    }
    if (!FsStoreRoot.empty()) {
        out << ToString(FsStoreStr) << "=" << FsStoreRoot << Endl;
    }
}

void NACCachePrivate::TConfigOptions::AddOptions(NLastGetopt::TOpts& parser) {
    parser.AddLongOption("ac-lock_file", "[local_cache.build_cache] lock_file")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&LockFile);
    parser.AddLongOption("ac-db_path", "[local_cache.build_cache] db_path")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&DBPath);
    parser.AddLongOption("ac-master_mode", "[local_cache.build_cache] master_mode")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&MasterMode);
    parser.AddLongOption("ac-cas_logging", "[local_cache.build_cache] cas_logging")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&CasLogging);
    parser.AddLongOption("ac-graph_info", "[local_cache.build_cache] graph_info")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&GraphInfo);
    parser.AddLongOption("ac-foreign_keys", "[local_cache.build_cache] foreign_keys")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&ForeignKeys);
    parser.AddLongOption("ac-no_db", "[local_cache.build_cache] no_db")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&NoDb);
    parser.AddLongOption("ac-no_blob_io", "[local_cache.build_cache] no_blob_io")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&NoBlobIO);
    parser.AddLongOption("ac-recreate_db", "[local_cache.build_cache] recreate_db")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&RecreateDB);
    parser.AddLongOption("ac-disk_limit", "[local_cache.build_cache] disk_limit")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DiskLimit);
    parser.AddLongOption("ac-quiescence_time", "[local_cache.build_cache] quiescence_time")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&QuiescenceTime);
    parser.AddLongOption("ac-cache_dir", "[local_cache.build_cache] cache_dir")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&FsStoreRoot);
}

static void MalformedEntryError(NACCachePrivate::EConfigStrings e) {
    Cerr << "Malformed '" << ToString(e) << "' entry in [" << NACCachePrivate::IniSectionName() << "] section" << Endl;
}

static void MissingEntryError(NACCachePrivate::EConfigStrings e) {
    Cerr << "Missing '" << ToString(e) << "' entry in [" << NACCachePrivate::IniSectionName() << "] section" << Endl;
}

static bool CheckFsPathParameter(const TString& s, NACCachePrivate::EConfigStrings e) {
    using namespace NACCachePrivate;
    switch (e) {
        case LockFileStr:
            return TFsPath(s).IsAbsolute();
        case DbPathStr: {
            if (s.StartsWith("file:")) {
                TStringBuf sbuf(s);
                TStringBuf scheme, path;
                sbuf.Split(':', scheme, path);
                return TFsPath(path).IsAbsolute();
            }
            return s.empty() || TFsPath(s).IsAbsolute();
        }
        case FsStoreStr:
            return TFsPath(s).IsAbsolute();
        default:
            break;
    }
    return false;
}

void NACCachePrivate::CheckConfig(const NConfig::TConfig& config) {
    using namespace NConfig;

    TDict acSection;
    try {
        acSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(BuildCacheStr)).Get<TDict>();
    } catch (const TTypeMismatch&) {
        Cerr << "Missing or malformed [" << IniSectionName() << "] section" << Endl;
        throw;
    }

    for (auto e : {FsStoreStr}) {
        if (!acSection.contains(ToString(e)) || acSection.At(ToString(e)).Get<TString>().empty()) {
            MissingEntryError(e);
            ythrow TTypeMismatch() << "Missing entry in ini file.";
        }
    }

    // TString + TFsPath
    for (auto e : {LockFileStr, DbPathStr, FsStoreStr}) {
        try {
            if (acSection.contains(ToString(e))) {
                auto& s = acSection.At(ToString(e)).Get<TString>();
                if (!CheckFsPathParameter(s, e)) {
                    ythrow TTypeMismatch() << "'" << ToString(e) << "' '" << s << "' should be absolute path";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // int
    for (auto e : {RunningMaxQueueStr, GcMaxQueueStr, QuiescenceStr, PollStr}) {
        try {
            if (acSection.contains(ToString(e))) {
                auto s = acSection.At(ToString(e)).As<int>();
                if (s < 0) {
                    ythrow TTypeMismatch() << "Negative value is not expected";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // i64
    for (auto e : {GcLimitStr}) {
        try {
            if (acSection.contains(ToString(e))) {
                auto s = acSection.At(ToString(e)).As<i64>();
                if (s < 0) {
                    ythrow TTypeMismatch() << "Negative value is not expected";
                }
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // bool
    for (auto e : {MasterModeStr, GraphInfoStr, NoDbStr, NoBlobIOStr, RecreateStr, CasLoggingStr, ForeignKeysStr}) {
        try {
            if (acSection.contains(ToString(e))) {
                acSection.At(ToString(e)).As<bool>();
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }
}

TString NACCachePrivate::GetDBDirectory(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& acSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(BuildCacheStr)).Get<TDict>();

    if (acSection.contains(ToString(DbPathStr))) {
        if (auto file = acSection.At(ToString(DbPathStr)).Get<TString>(); !file.empty()) {
            if (file.StartsWith("file:")) {
                // TODO: strip other URI-related components.
                file = file.substr(strlen("file:"));
            }
            return TFsPath(file).Dirname();
        }
    }
    if (auto cacheDir = acSection.At(ToString(FsStoreStr)).Get<TString>(); !cacheDir.empty()) {
        return cacheDir;
    }
    return "";
}

void NACCachePrivate::PrepareDirs(const NConfig::TConfig& config) {
    using namespace NConfig;
    TFsPath(TFsPath(GetLockName(config)).Dirname()).MkDirs(0755);
    TFsPath(GetDBDirectory(config)).MkDirs(0755);
}

TString NACCachePrivate::GetLockName(const NConfig::TConfig& config) {
    using namespace NConfig;
    const auto& section = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(BuildCacheStr)).Get<TDict>();

    if (section.contains(ToString(LockFileStr))) {
        return section.At(ToString(LockFileStr)).Get<TString>();
    }

    return JoinFsPaths(GetDBDirectory(config), ".cache_lock");
}

TString NACCachePrivate::GetCriticalErrorMarkerFileName(const NConfig::TConfig& config) {
    return JoinFsPaths(GetDBDirectory(config), "RECREATE_DB");
}
