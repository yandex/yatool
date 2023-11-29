#pragma once

#include <library/cpp/config/config.h>
#include <library/cpp/getopt/small/last_getopt.h>

#include <util/generic/maybe.h>
#include <util/string/cast.h>

namespace NToolsCachePrivate {
    enum EConfigStrings {
        // Should be in more generic place
        LocalCacheStr /* "local_cache" */,
        ToolsCacheStr /* "tools_cache" */,
        LockFileStr /* "lock_file" */,
        LogNameStr /* "log" */,
        DbPathStr /* "db_path" */,
        RunningMaxQueueStr /* "running_poller_queue_size" */,
        GcMaxQueueStr /* "gc_queue_size" */,
        MasterModeStr /* "master_mode" */,
        GcLimitStr /* "disk_limit" */,
        QuiescenceStr /* "quiescence_time" */,
        VersionStr /* "version" */,
        SandBoxIdStr /* "sb_id" */,
        SandBoxAltIdStr /* "sb_alt_id" */,
        SandBoxPathStr /* "sb_path" */,
        NoDbStr /* "no_db" */,
        PollStr /* "poll" */,
    };

    inline TString IniSectionName() {
        return ToString(LocalCacheStr) + "." + ToString(ToolsCacheStr);
    }

    // Selected options to override with command-line option.
    struct TConfigOptions {
        /// [local_cache.tools_cache] lock_file
        /// Deprecated, applies to all services.
        TString LockFile;
        /// [local_cache.tools_cache] log
        /// Deprecated, applies to all services.
        TMaybe<TString> LogName;
        /// [local_cache.tools_cache] db_path
        TString DBPath;
        /// [local_cache.tools_cache] master_mode
        TMaybe<bool> MasterMode;
        /// [local_cache.tools_cache] disk_limit
        TMaybe<i64> DiskLimit;
        /// [local_cache.tools_cache] quiescence_time
        TMaybe<i64> QuiescenceTime;
        /// [local_cache.tools_cache] version
        TMaybe<int> Version;
        /// [local_cache.tools_cache] sb_id
        TString SbId;
        /// [local_cache.tools_cache] sb_alt_id
        TString SbAltId;
        /// [local_cache.tools_cache] sb_path
        TString SbPath;

        bool HasParams() const;
        void WriteSection(IOutputStream& out) const;
        void AddOptions(NLastGetopt::TOpts& parser);
    };

    void CheckConfig(const NConfig::TConfig& config);

    void PrepareDirs(const NConfig::TConfig& config);

    TString GetDBDirectory(const NConfig::TConfig& config);

    // Deprecated, use GetLockName from psingleton/server/config.h
    TString GetLockName(const NConfig::TConfig& config);

    // Deprecated, use GetLockName from psingleton/server/config.h
    TString GetLogName(const NConfig::TConfig& config);
}
