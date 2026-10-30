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
        DistCacheUrlStr /* "dist_cache_url" */,
        DistCacheBatchSizeStr /* "dist_cache_batch_size" */,
        DistCacheFlushMsStr /* "dist_cache_flush_ms" */,
        DistCacheQueueItemsStr /* "dist_cache_queue_items" */,
        DistCacheQueueBytesStr /* "dist_cache_queue_bytes" */,
        DistCacheCooldownMsStr /* "dist_cache_cooldown_ms" */,
        DistCacheTimeoutMsStr /* "dist_cache_timeout_ms" */,
        DistCacheDrainMsStr /* "dist_cache_drain_ms" */,
        CodecStr /* "codec" */,
        CodecMinSizeStr /* "codec_min_size" */,
        CodecResultsStr /* "codec_results" */,
        CodecOnPutStr /* "codec_on_put" */,
        CodecPromoteOnGetStr /* "codec_promote_on_get" */,
        CodecMaxParallelStr /* "codec_max_parallel" */,
        CodecColdAgeStr /* "codec_cold_age" */,
        CodecColdIntervalStr /* "codec_cold_interval" */,
        CodecColdBatchStr /* "codec_cold_batch" */,
        DiskLimitKindStr /* "disk_limit_kind" */,
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
        TString DistCacheUrl;
        TMaybe<i64> DistCacheBatchSize;
        TMaybe<i64> DistCacheFlushMs;
        TMaybe<i64> DistCacheQueueItems;
        TMaybe<i64> DistCacheQueueBytes;
        TMaybe<i64> DistCacheCooldownMs;
        TMaybe<i64> DistCacheTimeoutMs;
        TMaybe<i64> DistCacheDrainMs;
        /// [local_cache.build_cache] codec: "" or zstd
        TMaybe<TString> Codec;
        /// [local_cache.build_cache] codec_min_size
        TMaybe<i64> CodecMinSize;
        /// [local_cache.build_cache] codec_results
        TMaybe<bool> CodecResults;
        /// [local_cache.build_cache] codec_on_put
        TMaybe<bool> CodecOnPut;
        /// [local_cache.build_cache] codec_promote_on_get
        TMaybe<bool> CodecPromoteOnGet;
        /// [local_cache.build_cache] codec_max_parallel
        TMaybe<i64> CodecMaxParallel;
        /// [local_cache.build_cache] codec_cold_age, seconds
        TMaybe<i64> CodecColdAge;
        /// [local_cache.build_cache] codec_cold_interval, seconds
        TMaybe<i64> CodecColdInterval;
        /// [local_cache.build_cache] codec_cold_batch
        TMaybe<i64> CodecColdBatch;
        /// [local_cache.build_cache] disk_limit_kind: fs or content
        TMaybe<TString> DiskLimitKind;

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
