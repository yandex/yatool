#include "config.h"

#include "devtools/local_cache/psingleton/server/config.h"
#include "devtools/local_cache/common/fs-utils/stats.h"

#include <util/folder/path.h>
#include <util/system/file.h>
#include <util/system/fstat.h>
#include <util/system/fs.h>
#include <util/string/printf.h>

#if defined(_linux_)
#include <unistd.h>
#endif
#include <util/generic/algorithm.h>
#include <util/generic/vector.h>

bool NACCachePrivate::TConfigOptions::HasParams() const {
    return LockFile != "" || DBPath != "" ||
           !MasterMode.Empty() || !GraphInfo.Empty() || !ForeignKeys.Empty() || !NoDb.Empty() ||
           !CasLogging.Empty() || !NoBlobIO.Empty() || !RecreateDB.Empty() ||
           !DiskLimit.Empty() || !QuiescenceTime.Empty() ||
           FsStoreRoot != "" || !DistCacheUrl.empty() ||
           !DistCacheBatchSize.Empty() || !DistCacheFlushMs.Empty() || !DistCacheQueueItems.Empty() || !DistCacheQueueBytes.Empty() || !DistCacheCooldownMs.Empty() || !DistCacheTimeoutMs.Empty() || !DistCacheDrainMs.Empty() ||
           !Codec.Empty() || !CodecMinSize.Empty() || !CodecResults.Empty() || !CodecOnPut.Empty() || !CodecPromoteOnGet.Empty() || !CodecMaxParallel.Empty() || !DiskLimitKind.Empty() ||
           !CodecColdAge.Empty() || !CodecColdInterval.Empty() || !CodecColdBatch.Empty();
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
    if (!DistCacheUrl.empty()) {
        out << ToString(DistCacheUrlStr) << "=" << DistCacheUrl << Endl;
    }
    if (auto* val = DistCacheBatchSize.Get()) {
        out << ToString(DistCacheBatchSizeStr) << "=" << *val << Endl;
    }
    if (auto* val = DistCacheFlushMs.Get()) {
        out << ToString(DistCacheFlushMsStr) << "=" << *val << Endl;
    }
    if (auto* val = DistCacheQueueItems.Get()) {
        out << ToString(DistCacheQueueItemsStr) << "=" << *val << Endl;
    }
    if (auto* val = DistCacheQueueBytes.Get()) {
        out << ToString(DistCacheQueueBytesStr) << "=" << *val << Endl;
    }
    if (auto* val = DistCacheCooldownMs.Get()) {
        out << ToString(DistCacheCooldownMsStr) << "=" << *val << Endl;
    }
    if (auto* val = DistCacheTimeoutMs.Get()) {
        out << ToString(DistCacheTimeoutMsStr) << "=" << *val << Endl;
    }
    if (auto* val = DistCacheDrainMs.Get()) {
        out << ToString(DistCacheDrainMsStr) << "=" << *val << Endl;
    }
    if (!FsStoreRoot.empty()) {
        out << ToString(FsStoreStr) << "=" << FsStoreRoot << Endl;
    }
    if (auto* val = Codec.Get()) {
        out << ToString(CodecStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecMinSize.Get()) {
        out << ToString(CodecMinSizeStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecResults.Get()) {
        out << ToString(CodecResultsStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecOnPut.Get()) {
        out << ToString(CodecOnPutStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecPromoteOnGet.Get()) {
        out << ToString(CodecPromoteOnGetStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecMaxParallel.Get()) {
        out << ToString(CodecMaxParallelStr) << "=" << *val << Endl;
    }
    if (auto* val = DiskLimitKind.Get()) {
        out << ToString(DiskLimitKindStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecColdAge.Get()) {
        out << ToString(CodecColdAgeStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecColdInterval.Get()) {
        out << ToString(CodecColdIntervalStr) << "=" << *val << Endl;
    }
    if (auto* val = CodecColdBatch.Get()) {
        out << ToString(CodecColdBatchStr) << "=" << *val << Endl;
    }
}

void NACCachePrivate::TConfigOptions::AddOptions(NLastGetopt::TOpts& parser) {
    parser.AddLongOption("ac-dist_cache_url", "Dist-cache telemetry url")
        .Hidden()
        .RequiredArgument()
        .StoreResult(&DistCacheUrl);
    parser.AddLongOption("ac-dist_cache_batch_size", "Dist-cache telemetry batch_size")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheBatchSize);
    parser.AddLongOption("ac-dist_cache_flush_ms", "Dist-cache telemetry flush_ms")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheFlushMs);
    parser.AddLongOption("ac-dist_cache_queue_items", "Dist-cache telemetry queue_items")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheQueueItems);
    parser.AddLongOption("ac-dist_cache_queue_bytes", "Dist-cache telemetry queue_bytes")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheQueueBytes);
    parser.AddLongOption("ac-dist_cache_cooldown_ms", "Dist-cache telemetry cooldown_ms")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheCooldownMs);
    parser.AddLongOption("ac-dist_cache_timeout_ms", "Dist-cache telemetry timeout_ms")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheTimeoutMs);
    parser.AddLongOption("ac-dist_cache_drain_ms", "Dist-cache telemetry drain_ms")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&DistCacheDrainMs);
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
    parser.AddLongOption("ac-codec", "[local_cache.build_cache] codec")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<TString>(&Codec);
    parser.AddLongOption("ac-codec_min_size", "[local_cache.build_cache] codec_min_size")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&CodecMinSize);
    parser.AddLongOption("ac-codec_results", "[local_cache.build_cache] codec_results")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&CodecResults);
    parser.AddLongOption("ac-codec_on_put", "[local_cache.build_cache] codec_on_put")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&CodecOnPut);
    parser.AddLongOption("ac-codec_promote_on_get", "[local_cache.build_cache] codec_promote_on_get")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<bool>(&CodecPromoteOnGet);
    parser.AddLongOption("ac-codec_max_parallel", "[local_cache.build_cache] codec_max_parallel")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&CodecMaxParallel);
    parser.AddLongOption("ac-disk_limit_kind", "[local_cache.build_cache] disk_limit_kind")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<TString>(&DiskLimitKind);
    parser.AddLongOption("ac-codec_cold_age", "[local_cache.build_cache] codec_cold_age")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&CodecColdAge);
    parser.AddLongOption("ac-codec_cold_interval", "[local_cache.build_cache] codec_cold_interval")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&CodecColdInterval);
    parser.AddLongOption("ac-codec_cold_batch", "[local_cache.build_cache] codec_cold_batch")
        .Hidden()
        .RequiredArgument()
        .StoreResultT<i64>(&CodecColdBatch);
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

    if (NUserServicePrivate::IsSharedMode(config) &&
        (!acSection.contains(ToString(LockFileStr)) || acSection.At(ToString(LockFileStr)).Get<TString>().empty())) {
        ythrow TTypeMismatch() << "--ac-shared-mode requires an explicit --ac-lock_file";
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
    for (auto e : {GcLimitStr, CodecMinSizeStr, CodecColdAgeStr}) {
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

    // positive int
    for (auto e : {CodecMaxParallelStr, CodecColdIntervalStr, CodecColdBatchStr}) {
        try {
            if (acSection.contains(ToString(e)) && acSection.At(ToString(e)).As<int>() <= 0) {
                ythrow TTypeMismatch() << "Positive value is expected";
            }
        } catch (const TTypeMismatch&) {
            MalformedEntryError(e);
            throw;
        } catch (const TFromStringException&) {
            MalformedEntryError(e);
            throw;
        }
    }

    // enumerations
    const std::pair<EConfigStrings, TVector<TString>> enums[] = {{CodecStr, {"", "zstd"}}, {DiskLimitKindStr, {"fs", "content"}}};
    for (const auto& [e, allowed] : enums) {
        if (acSection.contains(ToString(e)) && !IsIn(allowed, acSection.At(ToString(e)).Get<TString>())) {
            MalformedEntryError(e);
            ythrow TTypeMismatch() << "Unexpected value of '" << ToString(e) << "'";
        }
    }

    // bool
    for (auto e : {MasterModeStr, GraphInfoStr, NoDbStr, NoBlobIOStr, RecreateStr, CasLoggingStr, ForeignKeysStr, CodecResultsStr, CodecOnPutStr, CodecPromoteOnGetStr}) {
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
    if (NUserServicePrivate::IsSharedMode(config)) {
#if defined(_linux_)
        const auto lockName = GetLockName(config);
        const TFsPath lockDir = TFsPath(lockName).Dirname();
        const auto& acSection = config.Get<TDict>().At(ToString(LocalCacheStr)).Get<TDict>().At(ToString(BuildCacheStr)).Get<TDict>();
        const TFsPath cacheDir(acSection.At(ToString(FsStoreStr)).Get<TString>());

        auto validateLockFile = [](const TFsPath& path) {
            TFileStat stat(path);
            if (stat.IsNull() || !stat.IsFile()) {
                ythrow TIoException() << "shared-mode lock file has an unexpected type: " << path;
            }
            if (stat.Uid != static_cast<ui32>(geteuid()) || stat.Gid != static_cast<ui32>(getegid()) || (stat.Mode & 0777) != 0644) {
                ythrow TIoException() << "invalid shared-mode lock file ownership or permissions for " << path
                                      << ": expected uid=" << geteuid() << ", gid=" << getegid()
                                      << ", mode=0644"
                                      << ", got uid=" << stat.Uid << ", gid=" << stat.Gid
                                      << ", mode=" << Sprintf("%04o", stat.Mode & 0777);
            }
        };

        auto validateLockDirectory = [](const TFsPath& path) {
            TFileStat stat(path);
            if (stat.IsNull() || !stat.IsDir()) {
                ythrow TIoException() << "shared-mode lock directory has an unexpected type: " << path;
            }
            if (stat.Uid != static_cast<ui32>(geteuid()) || stat.Gid != static_cast<ui32>(getegid()) || (stat.Mode & 0777) != 0755) {
                ythrow TIoException() << "invalid shared-mode lock directory ownership or permissions for " << path
                                      << ": expected uid=" << geteuid() << ", gid=" << getegid() << ", mode=0755"
                                      << ", got uid=" << stat.Uid
                                      << ", gid=" << stat.Gid
                                      << ", mode=" << Sprintf("%04o", stat.Mode & 0777);
            }
        };

        validateLockDirectory(lockDir.RealPath());

        if (!cacheDir.Exists()) {
            cacheDir.MkDirs(0700);
            NFsPrivate::Chmod(cacheDir, 0700);
        }

        TFsPath dbDir(GetDBDirectory(config));
        if (!dbDir.Exists()) {
            dbDir.MkDirs(0700);
            NFsPrivate::Chmod(dbDir, 0700);
        }

        const TString ipc = lockDir.RealPath().GetPath();
        const TString cache = cacheDir.RealPath().GetPath();
        if (ipc == cache || ipc.StartsWith(cache + "/") || cache.StartsWith(ipc + "/")) {
            ythrow TIoException() << "shared-mode lock directory and cache_dir must be disjoint";
        }

        TFsPath lockPath(lockName);
        if (!lockPath.Exists()) {
            TFile(lockName, CreateNew | RdWr | ARUser | AWUser | ARGroup | AROther);
            // The daemon uses umask 0077 in shared mode, so make the
            // intentionally world-readable lock file explicit.
            NFsPrivate::Chmod(lockPath, 0644);
        }
        validateLockFile(lockPath.RealPath());
        return;
#else
        Y_UNREACHABLE();
#endif
    }
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
