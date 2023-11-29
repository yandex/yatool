#pragma once

#include <library/cpp/config/config.h>
#include <library/cpp/getopt/small/last_getopt.h>

#include <util/generic/maybe.h>
#include <util/string/cast.h>

namespace NACCachePrivate {
    enum EConfigStrings {
        // Should be in more generic place
        LocalCacheStr /* "local_cache" */,
        BuildCacheStr /* "build_cache" */,
        LockFileStr /* "lock_file" */,
        DbPathStr /* "db_path" */,
        RunningMaxQueueStr /* "running_poller_queue_size" */,
        GcMaxQueueStr /* "gc_queue_size" */,
        MasterModeStr /* "master_mode" */,
        CasLoggingStr /* "cas_logging" */,
        GcLimitStr /* "disk_limit" */,
        QuiescenceStr /* "quiescence_time" */,
        NoDbStr /* "no_db" */,
        NoBlobIOStr /* "no_blob_io" */,
        PollStr /* "poll" */,
        FsStoreStr /* "cache_dir" */,
        GraphInfoStr /* "graph_info" */,
        ForeignKeysStr /* "foreign_keys" */,
        RecreateStr /* "recreate_db" */,
    };

    inline TString IniSectionName() {
        return ToString(LocalCacheStr) + "." + ToString(BuildCacheStr);
    }

    // Selected options to override with command-line option.
    struct TConfigOptions {
        /// [local_cache.build_cache] lock_file
        TString LockFile;
        /// [local_cache.build_cache] db_path
        TString DBPath;
        /// [local_cache.build_cache] master_mode
        TMaybe<bool> MasterMode;
        /// [local_cache.build_cache] cas_logging
        TMaybe<bool> CasLogging;
        /// [local_cache.build_cache] graph_info
        TMaybe<bool> GraphInfo;
        /// [local_cache.build_cache] foreign_keys
        TMaybe<bool> ForeignKeys;
        /// [local_cache.build_cache] no_db
        TMaybe<bool> NoDb;
        /// [local_cache.build_cache] no_blob_io
        TMaybe<bool> NoBlobIO;
        /// [local_cache.build_cache] recreate_db
        TMaybe<bool> RecreateDB;
        /// [local_cache.build_cache] disk_limit
        TMaybe<i64> DiskLimit;
        /// [local_cache.build_cache] quiescence_time
        TMaybe<i64> QuiescenceTime;
        TString FsStoreRoot;

        bool HasParams() const;
        void WriteSection(IOutputStream& out) const;
        void AddOptions(NLastGetopt::TOpts& parser);
    };

    void CheckConfig(const NConfig::TConfig& config);

    TString GetDBDirectory(const NConfig::TConfig& config);

    TString GetLockName(const NConfig::TConfig& config);

    void PrepareDirs(const NConfig::TConfig& config);

    TString GetCriticalErrorMarkerFileName(const NConfig::TConfig& config);
}
